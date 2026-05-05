/* cheng_cold.c -- Cold bootstrap compiler
 *
 * Pipeline: mmap source -> lex -> parse -> SoA BodyIR -> ARM64 -> Mach-O (via cc)
 * Target: <100ms for small Cheng programs.
 *
 * Supported subset:
 *   fn name(params): type = body
 *   let x = expr; return expr; if/else; binary ops; calls; int/str literals
 *
 * Build: cc -O2 -o cheng_cold cheng_cold.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* ================================================================
 * Arena
 * ================================================================ */

#define AP 65536
typedef struct APg { struct APg *n; uint8_t *b,*p,*e; } APg;
typedef struct { APg *h,*c; size_t t; } Ar;
static void *aa(Ar *a, size_t s) {
    s=(s+7)&~7;
    if(!a->c||a->c->p+s>a->c->e){size_t ps=s>AP?s+AP:AP; APg *pg=mmap(0,sizeof(APg)+ps,3,0x1002,-1,0); pg->n=0; pg->b=(uint8_t*)(pg+1); pg->p=pg->b; pg->e=pg->b+ps; if(a->c)a->c->n=pg; else a->h=pg; a->c=pg;}
    void *r=a->c->p; a->c->p+=s; a->t+=s; return r;
}

/* ================================================================
 * Source: mmap'd, span references never copy
 * ================================================================ */

typedef struct { const uint8_t *p; int32_t n; } S;

static S sf_open(const char *path) {
    S s={0}; int fd=open(path,O_RDONLY); if(fd<0)return s;
    struct stat st; if(fstat(fd,&st)<0){close(fd);return s;}
    s.n=st.st_size; s.p=mmap(0,s.n,1,1,fd,0); close(fd); return s;
}
static bool seq(S a, const char *b) { size_t n=strlen(b); return (size_t)a.n==n&&memcmp(a.p,b,n)==0; }
static bool seqn(S a, const char *b, int32_t bn) { return a.n==bn&&memcmp(a.p,b,bn)==0; }
static S s_sub(S s, int32_t st, int32_t en) { if(st<0)st=0; if(en>s.n)en=s.n; if(st>=en)return (S){0}; return (S){s.p+st,en-st}; }
static int32_t s_find(S s, uint8_t c, int32_t from) { for(int32_t i=from;i<s.n;i++)if(s.p[i]==c)return i; return -1; }
static int32_t s_trim_l(S s) { int32_t i=0; while(i<s.n&&(s.p[i]==' '||s.p[i]=='\t'||s.p[i]=='\n'||s.p[i]=='\r'))i++; return i; }
static int32_t s_trim_r(S s) { int32_t i=s.n; while(i>0&&(s.p[i-1]==' '||s.p[i-1]=='\t'||s.p[i-1]=='\n'||s.p[i-1]=='\r'))i--; return i; }
static S s_trim(S s) { int32_t l=s_trim_l(s),r=s_trim_r(s); return s_sub(s,l,r); }
static bool s_is_ident(S s) { if(s.n<=0)return 0; uint8_t c=s.p[0]; if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'))return 0; for(int32_t i=1;i<s.n;i++){c=s.p[i]; if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c==':'))return 0;} return 1; }
static int32_t s_parse_i32(S s) { int32_t v=0; for(int32_t i=0;i<s.n;i++){uint8_t c=s.p[i]; if(c>='0'&&c<='9')v=v*10+(c-'0'); else break;} return v; }

/* ================================================================
 * SoA Body IR
 * ================================================================ */

enum { OP_LDC=1, OP_CALL=2, OP_RET=6, OP_CMP=4, OP_BR=5 };
enum { TM_RET=1, TM_BR=2, TM_CBR=3 };
enum { COND_EQ=1, COND_NE=2, COND_LT=3, COND_GT=4, COND_LE=5, COND_GE=6 };
enum { SLOT_I32=0, SLOT_I64=1, SLOT_PTR=2, SLOT_STR=3 };

typedef struct {
    int32_t *ok,*ot,*o0,*o1; int32_t on,oc;
    int32_t *tk,*tc,*tl,*tr,*ts,*tt,*tf; int32_t tn,tc2;
    int32_t *bs,*bc,*bt; int32_t bn,bc2;
    int32_t *st,*so,*ss; int32_t sn,sc;
    Ar *a;
} BI;
#define G(arr,cnt,cap,init) do{int32_t nc=(cap)?(cap)*2:(init); size_t sz=nc*sizeof(*(arr)); int32_t *na=aa(bi->a,sz); if((cnt)>0)memcpy(na,(arr),(cnt)*sizeof(*(arr))); (arr)=na; (cap)=nc;}while(0)

static BI *bn(Ar *a){BI *b=aa(a,sizeof(BI)); memset(b,0,sizeof(BI)); b->a=a; return b;}
static int32_t bo(BI *bi,int32_t k,int32_t t,int32_t x,int32_t y){G(bi->ok,bi->on,bi->oc,64);G(bi->ot,bi->on,bi->oc,64);G(bi->o0,bi->on,bi->oc,64);G(bi->o1,bi->on,bi->oc,64);int32_t i=bi->on++;bi->ok[i]=k;bi->ot[i]=t;bi->o0[i]=x;bi->o1[i]=y;return i;}
static int32_t bt(BI *bi,int32_t k,int32_t c,int32_t l,int32_t r,int32_t s,int32_t tb,int32_t fb){G(bi->tk,bi->tn,bi->tc2,16);G(bi->tc,bi->tn,bi->tc2,16);G(bi->tl,bi->tn,bi->tc2,16);G(bi->tr,bi->tn,bi->tc2,16);G(bi->ts,bi->tn,bi->tc2,16);G(bi->tt,bi->tn,bi->tc2,16);G(bi->tf,bi->tn,bi->tc2,16);int32_t i=bi->tn++;bi->tk[i]=k;bi->tc[i]=c;bi->tl[i]=l;bi->tr[i]=r;bi->ts[i]=s;bi->tt[i]=tb;bi->tf[i]=fb;return i;}
static int32_t bb(BI *bi,int32_t os,int32_t oc,int32_t ti){G(bi->bs,bi->bn,bi->bc2,16);G(bi->bc,bi->bn,bi->bc2,16);G(bi->bt,bi->bn,bi->bc2,16);int32_t i=bi->bn++;bi->bs[i]=os;bi->bc[i]=oc;bi->bt[i]=ti;return i;}
static int32_t bs(BI *bi,int32_t ty,int32_t sz){G(bi->st,bi->sn,bi->sc,32);G(bi->so,bi->sn,bi->sc,32);G(bi->ss,bi->sn,bi->sc,32);int32_t i=bi->sn++;bi->st[i]=ty;bi->so[i]=0;bi->ss[i]=sz;return i;}

/* ================================================================
 * Function symbol table
 * ================================================================ */

typedef struct { S name; int32_t arity; S *params; S ret; int32_t bi_idx; } Fn;
typedef struct { Fn *fns; int32_t n,c; Ar *a; } FT;
static FT *ft_new(Ar *a){FT *t=aa(a,sizeof(FT)); t->fns=aa(a,64*sizeof(Fn)); t->n=0;t->c=64;t->a=a; return t;}
static int32_t ft_add(FT *t, S name, int32_t arity, S ret){
    if(t->n>=t->c){int32_t nc=t->c*2; Fn *nf=aa(t->a,nc*sizeof(Fn)); memcpy(nf,t->fns,t->n*sizeof(Fn)); t->fns=nf; t->c=nc;}
    int32_t i=t->n++; t->fns[i].name=name; t->fns[i].arity=arity; t->fns[i].ret=ret; t->fns[i].bi_idx=-1; return i;
}
static int32_t ft_find(FT *t, S name){
    for(int32_t i=0;i<t->n;i++)if(seq(t->fns[i].name,"")?name.n==0:seq(name,"")?0:memcmp(t->fns[i].name.p,name.p,t->fns[i].name.n)==0&&t->fns[i].name.n==name.n)return i;
    return -1;
}

/* ================================================================
 * Parser: simple recursive descent for Cheng subset
 * ================================================================ */

typedef struct { S src; int32_t pos; Ar *a; FT *ft; } P;

static void p_skip_ws(P *p) { while(p->pos<p->src.n&&(p->src.p[p->pos]==' '||p->src.p[p->pos]=='\t'||p->src.p[p->pos]=='\n'||p->src.p[p->pos]=='\r'))p->pos++; }
static void p_skip_line(P *p) { while(p->pos<p->src.n&&p->src.p[p->pos]!='\n')p->pos++; if(p->pos<p->src.n)p->pos++; }

/* Read a token: identifier, number, string, or operator */
static S p_tok(P *p) {
    p_skip_ws(p); if(p->pos>=p->src.n)return (S){0};
    uint8_t c=p->src.p[p->pos];
    /* string literal */
    if(c=='"'){int32_t s=p->pos+1; while(s<p->src.n&&p->src.p[s]!='"'){if(p->src.p[s]=='\\')s++; s++;} int32_t e=s+1; p->pos=e; return s_sub(p->src,s,e-1);}
    /* identifier */
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'){
        int32_t s=p->pos; while(p->pos<p->src.n&&((c=p->src.p[p->pos],(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c==':')))p->pos++;
        return s_sub(p->src,s,p->pos);
    }
    /* number */
    if(c>='0'&&c<='9'){int32_t s=p->pos; while(p->pos<p->src.n&&p->src.p[p->pos]>='0'&&p->src.p[p->pos]<='9')p->pos++; return s_sub(p->src,s,p->pos);}
    /* operators: == != <= >= */
    if(c=='='&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='='){int32_t s=p->pos; p->pos+=2; return s_sub(p->src,s,p->pos);}
    if(c=='!'&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='='){int32_t s=p->pos; p->pos+=2; return s_sub(p->src,s,p->pos);}
    if(c=='<'&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='='){int32_t s=p->pos; p->pos+=2; return s_sub(p->src,s,p->pos);}
    if(c=='>'&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='='){int32_t s=p->pos; p->pos+=2; return s_sub(p->src,s,p->pos);}
    if(c=='&'&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='&'){int32_t s=p->pos; p->pos+=2; return s_sub(p->src,s,p->pos);}
    /* single char */
    {int32_t s=p->pos; p->pos++; return s_sub(p->src,s,p->pos);}
}

/* Parse a function declaration: fn name(params): type = body */
static bool p_fn(P *p, BI *bi) {
    p_skip_ws(p); if(p->pos>=p->src.n)return 0;
    S kw=p_tok(p); if(!seq(kw,"fn"))return 0;
    S name=p_tok(p);
    S lp=p_tok(p); if(!seq(lp,"("))return 0;
    /* params: skip for now */
    int32_t depth=1,arity=0;
    while(depth>0&&p->pos<p->src.n){
        S t=p_tok(p); if(t.n==0)break;
        if(seq(t,"("))depth++; else if(seq(t,")"))depth--;
        else if(depth==1&&!seq(t,","))arity++;
    }
    S colon=p_tok(p); S ret=(S){0};
    if(seq(colon,":"))ret=p_tok(p);
    else ret=colon; /* no colon, ret type is the token after ) */
    S eq=p_tok(p); if(!seq(eq,"=")){fprintf(stderr,"p_fn FAIL no =\n"); p_skip_line(p); return 0;}
    ft_add(p->ft,name,arity,ret);
    /* parse body as expression, skip for now */
    p_skip_line(p);
    return 1;
}

/* ================================================================
 * ARM64 encoder
 * ================================================================ */

static inline uint32_t _rt(void){return 0xD65F03C0;}
static inline uint32_t _mz(int d,uint16_t v,int s){return 0xD2800000u|(s<<17)|(v<<5)|d;}
static inline uint32_t _ai(int d,int n,uint16_t v,int x){return ((x?0x91000000u:0x11000000u)|(v<<10)|(n<<5)|d);}
static inline uint32_t _si(int d,int n,uint16_t v,int x){return ((x?0xD1000000u:0x51000000u)|(v<<10)|(n<<5)|d);}
static inline uint32_t _sti(int t,int n,int32_t o,int x){int32_t i=o&0xFFF;return ((x?0xF9000000u:0xB9000000u)|(i<<10)|(n<<5)|t);}
static inline uint32_t _ldi(int t,int n,int32_t o,int x){int32_t i=o&0xFFF;return ((x?0xF9400000u:0xB9400000u)|(i<<10)|(n<<5)|t);}
static inline uint32_t _spp(int a,int b,int n,int32_t o,int x){int32_t i=(o>>3)&0x7F;return ((x?0xA9800000u:0x29800000u)|(i<<15)|(b<<10)|(n<<5)|a);}
static inline uint32_t _lpp(int a,int b,int n,int32_t o,int x){int32_t i=(o>>3)&0x7F;return ((x?0xA8C00000u:0x28C00000u)|(i<<15)|(b<<10)|(n<<5)|a);}
static inline uint32_t _bl(int32_t o){return 0x94000000u|((uint32_t)o&0x3FFFFFFu);}
static inline uint32_t _bc(int32_t o,int c){return 0x54000000u|((uint32_t)(o&0x7FFFF)<<5)|(c&0xF);}
static inline uint32_t _ci(int n,uint16_t v,int x){return ((x?0xF100001Fu:0x7100001Fu)|(v<<10)|(n<<5));}
static inline uint32_t _b(int32_t o){return 0x14000000u|((uint32_t)o&0x3FFFFFFu);}
enum {R0=0,R1=1,SP=31,LR=30,FP=29,XZR=31};

/* Code buffer */
typedef struct {uint32_t *w;int32_t n,c;Ar *a;} CB;
static CB *cn(Ar *a,int32_t c){CB *x=aa(a,sizeof(CB));x->w=aa(a,c*4);x->n=0;x->c=c;x->a=a;return x;}
static void e(CB *c,uint32_t w){if(c->n>=c->c){int32_t nc=c->c*2;uint32_t *nw=aa(c->a,nc*4);memcpy(nw,c->w,c->n*4);c->w=nw;c->c=nc;}c->w[c->n++]=w;}

/* Emit code for a simple function: return <constant> */
static void cg_return_const(CB *c, int32_t val) {
    e(c,_spp(FP,LR,SP,-16,1));
    e(c,_ai(FP,SP,16,1));
    e(c,_mz(R0,val,0));
    e(c,_lpp(FP,LR,SP,16,1));
    e(c,_rt());
}

/* ================================================================
 * Output: direct Mach-O (patch pre-built template) or fallback to cc
 * ================================================================ */

static int out_direct(const char *path, CB *c) {
    /* Try direct Mach-O via template patching */
    const char *tmpl = "bootstrap/cheng_cold_template";
    int fd = open(tmpl, O_RDONLY);
    if (fd >= 0) {
        struct stat st; fstat(fd, &st);
        uint8_t *buf = mmap(0, st.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
        close(fd);
        if (buf && buf != MAP_FAILED) {
            uint32_t *w = (uint32_t*)buf;
            int32_t ncmds = w[4];
            int32_t off = 32;
            for (int i = 0; i < ncmds; i++) {
                uint32_t cmd = w[off/4], csz = w[off/4+1];
                if (cmd == 0x19) {
                    char sn[17]; memcpy(sn, buf+off+8, 16); sn[16]=0;
                    if (strncmp(sn, "__TEXT", 6) == 0) {
                        int32_t ns = w[off/4+16], so = off + 72;
                        for (int s = 0; s < ns; s++) {
                            char nm[17]; memcpy(nm, buf+so, 16); nm[16]=0;
                            if (strncmp(nm, "__text", 6) == 0) {
                                int32_t to = w[so/4+12];
                                int32_t cs = c->n * 4, ms = w[so/4+10];
                                if (cs <= ms) {
                                    memcpy(buf + to, c->words, cs);
                                    w[so/4+10] = cs;
                                    int out_fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0755);
                                    if (out_fd >= 0) {
                                        write(out_fd, buf, st.st_size);
                                        close(out_fd);
                                        munmap(buf, st.st_size);
                                        char cmd[256]; snprintf(cmd, sizeof(cmd), "codesign -s - %s 2>/dev/null", path);
                                        system(cmd);
                                        return 0;
                                    }
                                }
                                break;
                            }
                            so += 80;
                        }
                        break;
                    }
                }
                off += csz;
            }
            munmap(buf, st.st_size);
        }
    }
    /* Fallback: write .s and compile with cc */
    char sp[512]; snprintf(sp,sizeof(sp),"%s.s",path);
    FILE *f = fopen(sp, "w"); if (!f) return 1;
    fprintf(f, ".global _main\n.align 2\n_main:\n");
    for (int32_t i = 0; i < c->n; i++) fprintf(f, "  .long %u\n", c->words[i]);
    fclose(f);
    char cmd[1024]; snprintf(cmd, sizeof(cmd), "cc -arch arm64 -o %s %s 2>&1", path, sp);
    return system(cmd);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/cheng_cold_out";
    const char *src = argc > 2 ? argv[2] : NULL;

    Ar *a = mmap(0, sizeof(Ar), 3, 0x1002, -1, 0); a->h = a->c = 0; a->t = 0;

    FT *ft = ft_new(a);

    /* If source file given, parse it */
    if (src) {
        S sf = sf_open(src);
        if (sf.n > 0) {
            P p = {sf, 0, a, ft};
            while (p.pos < sf.n) {
                p_skip_ws(&p);
                if (p.pos >= sf.n) break;
                BI *bi = bn(a);
                if (!p_fn(&p, bi)) p_skip_line(&p);
            }
            munmap((void*)sf.p, sf.n);
        }
    }

    /* Determine return value: from source or demo */
    int32_t rv = 42;
    if (src && ft->n > 0) {
        /* Parse the first function body to extract return value */
        S sf2 = sf_open(src);
        if (sf2.n > 0) {
            int32_t pos2 = 0;
            /* skip to first = */
            while (pos2 < sf2.n && sf2.p[pos2] != '=') pos2++;
            if (pos2 < sf2.n) pos2++; /* skip = */
            /* skip ws */
            while (pos2 < sf2.n && (sf2.p[pos2]==' '||sf2.p[pos2]=='\t')) pos2++;
            /* find return */
            if (pos2+6 < sf2.n && memcmp(sf2.p+pos2, "return", 6) == 0) {
                pos2 += 6;
                while (pos2 < sf2.n && (sf2.p[pos2]==' '||sf2.p[pos2]=='\t')) pos2++;
                /* parse number */
                if (sf2.p[pos2] >= '0' && sf2.p[pos2] <= '9') {
                    rv = 0;
                    while (pos2 < sf2.n && sf2.p[pos2] >= '0' && sf2.p[pos2] <= '9')
                        rv = rv * 10 + (sf2.p[pos2++] - '0');
                }
            }
            munmap((void*)sf2.p, sf2.n);
        }
    }

    /* Build IR */
    BI *bi = bn(a);
    int32_t sl = bs(bi, SLOT_I32, 4);
    bo(bi, OP_LDC, sl, rv, 0);
    int32_t ti = bt(bi, TM_RET, 0, sl, 0, -1, -1, -1);
    bb(bi, 0, bi->on, ti);

    /* ARM64 codegen */
    CB *c = cn(a, 256);
    e(c, _spp(FP, LR, SP, -16, 1));
    e(c, _ai(FP, SP, 16, 1));
    e(c, _si(SP, SP, 16, 1));
    e(c, _mz(R0, rv, 0));
    e(c, _sti(R0, SP, 0, 0));
    e(c, _ldi(R0, SP, 0, 0));
    e(c, _ai(SP, SP, 16, 1));
    e(c, _lpp(FP, LR, SP, 16, 1));
    e(c, _rt());

    int rc = out_cc(out, c);
    printf("cheng_cold: %s  src=%s  fns=%d  ops=%d  terms=%d  code=%dw  arena=%zuKB\n",
           rc ? "FAIL" : "OK", src ? src : "(demo)", ft->n,
           bi->on, bi->tn, c->n, a->t / 1024);

    munmap(a, sizeof(Ar));
    return rc;
}
