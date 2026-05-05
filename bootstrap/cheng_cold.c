/* cheng_cold.c -- Cold bootstrap compiler
 *
 * Minimal Cheng subset: fn, type(algebraic), let, return, if/else, match,
 * binary ops, calls, int/str literals.
 *
 * Pipeline: mmap source -> lex -> parse -> SoA BodyIR -> ARM64 -> Mach-O (via cc)
 * Target: <100ms cold bootstrap.
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
 * Source: mmap'd, span refs never copy
 * ================================================================ */
typedef struct { const uint8_t *p; int32_t n; } S;
static S sf_open(const char *path){S s={0};int fd=open(path,O_RDONLY);if(fd<0)return s;struct stat st;if(fstat(fd,&st)<0){close(fd);return s;}s.n=st.st_size;s.p=mmap(0,s.n,1,1,fd,0);close(fd);return s;}
static bool seq(S a,const char *b){size_t n=strlen(b);return (size_t)a.n==n&&memcmp(a.p,b,n)==0;}
static S s_sub(S s,int32_t st,int32_t en){if(st<0)st=0;if(en>s.n)en=s.n;if(st>=en)return (S){0};return (S){s.p+st,en-st};}
static int32_t s_parse_i32(S s){int32_t v=0;for(int32_t i=0;i<s.n;i++){uint8_t c=s.p[i];if(c>='0'&&c<='9')v=v*10+(c-'0');else break;}return v;}

/* ================================================================
 * SoA Body IR
 * ================================================================ */
enum { OP_LDC=1,OP_CALL=2,OP_BINOP=3,OP_CMP=4,OP_BR=5,OP_RET=6,OP_TAG=7,OP_PAYLOAD=8 };
enum { TM_RET=1,TM_BR=2,TM_CBR=3,TM_SWITCH=4 };
enum { COND_EQ=1,COND_NE=2,COND_LT=3,COND_GT=4,COND_LE=5,COND_GE=6 };
enum { BIN_ADD=1,BIN_SUB=2,BIN_MUL=3,BIN_DIV=4 };
enum { SLOT_I32=0,SLOT_I64=1,SLOT_PTR=2,SLOT_STR=3,SLOT_TAG=4 };

typedef struct {
    int32_t *ok,*ot,*o0,*o1; int32_t on,oc;
    int32_t *tk,*tc,*tl,*tr,*ts,*tt,*tf; int32_t tn,tc2;
    int32_t *bs,*bc,*bt; int32_t bn,bc2;
    int32_t *st,*so,*ss; int32_t sn,sc;
    Ar *a;
} BI;
#define G(arr,cnt,cap,init) do{int32_t nc=(cap)?(cap)*2:(init);size_t sz=nc*sizeof(*(arr));int32_t*na=aa(bi->a,sz);if((cnt)>0)memcpy(na,(arr),(cnt)*sizeof(*(arr)));(arr)=na;(cap)=nc;}while(0)

static BI *bn(Ar *a){BI *b=aa(a,sizeof(BI));memset(b,0,sizeof(BI));b->a=a;return b;}
static int32_t bo(BI *bi,int32_t k,int32_t t,int32_t x,int32_t y){G(bi->ok,bi->on,bi->oc,64);G(bi->ot,bi->on,bi->oc,64);G(bi->o0,bi->on,bi->oc,64);G(bi->o1,bi->on,bi->oc,64);int32_t i=bi->on++;bi->ok[i]=k;bi->ot[i]=t;bi->o0[i]=x;bi->o1[i]=y;return i;}
static int32_t bt(BI *bi,int32_t k,int32_t c,int32_t l,int32_t r,int32_t s,int32_t tb,int32_t fb){G(bi->tk,bi->tn,bi->tc2,16);G(bi->tc,bi->tn,bi->tc2,16);G(bi->tl,bi->tn,bi->tc2,16);G(bi->tr,bi->tn,bi->tc2,16);G(bi->ts,bi->tn,bi->tc2,16);G(bi->tt,bi->tn,bi->tc2,16);G(bi->tf,bi->tn,bi->tc2,16);int32_t i=bi->tn++;bi->tk[i]=k;bi->tc[i]=c;bi->tl[i]=l;bi->tr[i]=r;bi->ts[i]=s;bi->tt[i]=tb;bi->tf[i]=fb;return i;}
static int32_t bb(BI *bi,int32_t os,int32_t oc,int32_t ti){G(bi->bs,bi->bn,bi->bc2,16);G(bi->bc,bi->bn,bi->bc2,16);G(bi->bt,bi->bn,bi->bc2,16);int32_t i=bi->bn++;bi->bs[i]=os;bi->bc[i]=oc;bi->bt[i]=ti;return i;}
static int32_t bs(BI *bi,int32_t ty,int32_t sz){G(bi->st,bi->sn,bi->sc,32);G(bi->so,bi->sn,bi->sc,32);G(bi->ss,bi->sn,bi->sc,32);int32_t i=bi->sn++;bi->st[i]=ty;bi->so[i]=0;bi->ss[i]=sz;return i;}

/* ================================================================
 * Symbol table
 * ================================================================ */
typedef struct { S name; int32_t arity; S ret; int32_t bi_idx; } Fn;
typedef struct { S name; int32_t tag; int32_t *field_types; S *field_names; int32_t n_fields; } Varnt;
typedef struct { S name; Varnt *vs; int32_t nv; } Type;
typedef struct { Fn *fns; int32_t n,c; Type *types; int32_t tn,tc; Ar *a; } ST;
static ST *st_new(Ar *a){ST *t=aa(a,sizeof(ST));t->fns=aa(a,64*sizeof(Fn));t->n=0;t->c=64;t->types=aa(a,32*sizeof(Type));t->tn=0;t->tc=32;t->a=a;return t;}
static int32_t st_add_fn(ST *st,S name,int32_t arity,S ret){if(st->n>=st->c){int32_t nc=st->c*2;Fn *nf=aa(st->a,nc*sizeof(Fn));memcpy(nf,st->fns,st->n*sizeof(Fn));st->fns=nf;st->c=nc;}int32_t i=st->n++;st->fns[i].name=name;st->fns[i].arity=arity;st->fns[i].ret=ret;st->fns[i].bi_idx=-1;return i;}
static int32_t st_find_fn(ST *st,S name){for(int32_t i=0;i<st->n;i++)if(st->fns[i].name.n==name.n&&memcmp(st->fns[i].name.p,name.p,name.n)==0)return i;return -1;}
static int32_t st_add_type(ST *st,S name){if(st->tn>=st->tc){int32_t nc=st->tc*2;Type *nt=aa(st->a,nc*sizeof(Type));memcpy(nt,st->types,st->tn*sizeof(Type));st->types=nt;st->tc=nc;}int32_t i=st->tn++;st->types[i].name=name;st->types[i].vs=0;st->types[i].nv=0;return i;}
static int32_t st_find_type(ST *st,S name){for(int32_t i=0;i<st->tn;i++)if(st->types[i].name.n==name.n&&memcmp(st->types[i].name.p,name.p,name.n)==0)return i;return -1;}

/* ================================================================
 * Parser: simple recursive descent
 * ================================================================ */
typedef struct { S src; int32_t pos; Ar *a; ST *st; } P;
static void p_ws(P *p){while(p->pos<p->src.n&&(p->src.p[p->pos]==' '||p->src.p[p->pos]=='\t'||p->src.p[p->pos]=='\n'||p->src.p[p->pos]=='\r'))p->pos++;}
static void p_line(P *p){while(p->pos<p->src.n&&p->src.p[p->pos]!='\n')p->pos++;if(p->pos<p->src.n)p->pos++;}

static S p_tok(P *p){
    p_ws(p);if(p->pos>=p->src.n)return (S){0};
    uint8_t c=p->src.p[p->pos];
    if(c=='"'){int32_t s=p->pos+1;while(s<p->src.n&&p->src.p[s]!='"'){if(p->src.p[s]=='\\')s++;s++;}int32_t e=s+1;p->pos=e;return s_sub(p->src,s,e-1);}
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'){int32_t s=p->pos;while(p->pos<p->src.n&&((c=p->src.p[p->pos],(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c==':')))p->pos++;return s_sub(p->src,s,p->pos);}
    if(c>='0'&&c<='9'){int32_t s=p->pos;while(p->pos<p->src.n&&p->src.p[p->pos]>='0'&&p->src.p[p->pos]<='9')p->pos++;return s_sub(p->src,s,p->pos);}
    if(c=='='&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='='){int32_t s=p->pos;p->pos+=2;return s_sub(p->src,s,p->pos);}
    if(c=='!'&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='='){int32_t s=p->pos;p->pos+=2;return s_sub(p->src,s,p->pos);}
    if(c=='<'&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='='){int32_t s=p->pos;p->pos+=2;return s_sub(p->src,s,p->pos);}
    if(c=='>'&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='='){int32_t s=p->pos;p->pos+=2;return s_sub(p->src,s,p->pos);}
    if(c=='&'&&p->pos+1<p->src.n&&p->src.p[p->pos+1]=='&'){int32_t s=p->pos;p->pos+=2;return s_sub(p->src,s,p->pos);}
    if(c=='|'){int32_t s=p->pos;p->pos++;return s_sub(p->src,s,p->pos);}
    {int32_t s=p->pos;p->pos++;return s_sub(p->src,s,p->pos);}
}

/* Peek at next token without consuming */
static S p_peek(P *p){int32_t saved=p->pos;S t=p_tok(p);p->pos=saved;return t;}

/* Parse type declaration: type Name = Variant(field:type) | Variant2 */
static bool p_type(P *p){
    S kw=p_tok(p);if(!seq(kw,"type"))return false;
    S name=p_tok(p);
    S eq=p_tok(p);if(!seq(eq,"=")){p_line(p);return false;}
    int32_t ti=st_add_type(p->st,name);
    /* Count variants first */
    int32_t saved=p->pos,nv=0;
    while(p->pos<p->src.n){S t=p_tok(p);if(t.n==0||seq(t,"\n"))break;if(seq(t,"|"))nv++;}
    p->pos=saved;
    p->st->types[ti].nv=nv+1;
    p->st->types[ti].vs=aa(p->a,(nv+1)*sizeof(Varnt));
    for(int32_t vi=0;vi<=nv;vi++){
        if(vi>0){S bar=p_tok(p);}/* skip | */
        S vname=p_tok(p);
        p->st->types[ti].vs[vi].name=vname;
        p->st->types[ti].vs[vi].tag=vi;
        S lp=p_tok(p);
        if(seq(lp,"(")){
            int32_t depth=1,nf=0;
            while(depth>0&&p->pos<p->src.n){S t=p_tok(p);if(t.n==0)break;if(seq(t,"("))depth++;else if(seq(t,")"))depth--;else if(depth==1&&!seq(t,",")&&!seq(t,":"))nf++;}
            p->st->types[ti].vs[vi].n_fields=nf/2; /* name:type pairs */
        }
    }
    p_line(p);return true;
}

/* Parse function declaration: fn name(params): type = body */
static bool p_fn(P *p,BI *bi){
    S kw=p_tok(p);if(!seq(kw,"fn"))return false;
    S name=p_tok(p);
    S lp=p_tok(p);if(!seq(lp,"(")){p_line(p);return false;}
    int32_t depth=1,arity=0;
    while(depth>0&&p->pos<p->src.n){S t=p_tok(p);if(t.n==0)break;if(seq(t,"("))depth++;else if(seq(t,")"))depth--;else if(depth==1&&!seq(t,",")&&!seq(t,":"))arity++;}
    S colon=p_tok(p);S ret=(S){0};
    if(seq(colon,":"))ret=p_tok(p);else ret=colon;
    S eq=p_tok(p);if(!seq(eq,"=")){p_line(p);return false;}
    st_add_fn(p->st,name,arity,ret);
    /* Parse body: return <expr> */
    S kw2=p_tok(p);
    if(seq(kw2,"return")){
        S val=p_tok(p);
        int32_t rv=s_parse_i32(val);
        int32_t sl=bs(bi,SLOT_I32,4);
        bo(bi,OP_LDC,sl,rv,0);
        bt(bi,TM_RET,0,sl,0,-1,-1,-1);
        bb(bi,0,bi->on,bi->tn-1);
    } else if(seq(kw2,"let")){
        /* let x = <expr> - skip for now */
        p_line(p);return true;
    } else if(seq(kw2,"if")){
        /* if/else - skip for now */
        p_line(p);return true;
    } else if(seq(kw2,"match")){
        /* match - skip for now */
        p_line(p);return true;
    }
    p_line(p);return true;
}

/* ================================================================
 * ARM64 encoder
 * ================================================================ */
static inline uint32_t _rt(void){return 0xD65F03C0;}
static inline uint32_t _mz(int d,uint16_t v,int s){return 0xD2800000u|(s<<17)|(v<<5)|d;}
static inline uint32_t _ai(int d,int n,uint16_t v,int x){return((x?0x91000000u:0x11000000u)|(v<<10)|(n<<5)|d);}
static inline uint32_t _si(int d,int n,uint16_t v,int x){return((x?0xD1000000u:0x51000000u)|(v<<10)|(n<<5)|d);}
static inline uint32_t _sti(int t,int n,int32_t o,int x){int32_t i=o&0xFFF;return((x?0xF9000000u:0xB9000000u)|(i<<10)|(n<<5)|t);}
static inline uint32_t _ldi(int t,int n,int32_t o,int x){int32_t i=o&0xFFF;return((x?0xF9400000u:0xB9400000u)|(i<<10)|(n<<5)|t);}
static inline uint32_t _spp(int a,int b,int n,int32_t o,int x){int32_t i=(o>>3)&0x7F;return((x?0xA9800000u:0x29800000u)|(i<<15)|(b<<10)|(n<<5)|a);}
static inline uint32_t _lpp(int a,int b,int n,int32_t o,int x){int32_t i=(o>>3)&0x7F;return((x?0xA8C00000u:0x28C00000u)|(i<<15)|(b<<10)|(n<<5)|a);}
static inline uint32_t _bl(int32_t o){return 0x94000000u|((uint32_t)o&0x3FFFFFFu);}
static inline uint32_t _bc(int32_t o,int c){return 0x54000000u|((uint32_t)(o&0x7FFFF)<<5)|(c&0xF);}
static inline uint32_t _ci(int n,uint16_t v,int x){return((x?0xF100001Fu:0x7100001Fu)|(v<<10)|(n<<5));}
static inline uint32_t _b(int32_t o){return 0x14000000u|((uint32_t)o&0x3FFFFFFu);}
static inline uint32_t _add(int d,int n,int m,int x){return((x?0x8B000000u:0x0B000000u)|(m<<16)|(n<<5)|d);}
static inline uint32_t _sub(int d,int n,int m,int x){return((x?0xCB000000u:0x4B000000u)|(m<<16)|(n<<5)|d);}
static inline uint32_t _mul(int d,int n,int m,int x){return((x?0x9B007C00u:0x1B007C00u)|(m<<16)|(n<<5)|d);}
static inline uint32_t _sdiv(int d,int n,int m,int x){return((x?0x9AC00C00u:0x1AC00C00u)|(m<<16)|(n<<5)|d);}
enum {R0=0,R1=1,R2=2,SP=31,LR=30,FP=29,XZR=31};

/* Code buffer */
typedef struct {uint32_t *w;int32_t n,c;Ar *a;} CB;
static CB *cn(Ar *a,int32_t c){CB *x=aa(a,sizeof(CB));x->w=aa(a,c*4);x->n=0;x->c=c;x->a=a;return x;}
static void e(CB *c,uint32_t w){if(c->n>=c->c){int32_t nc=c->c*2;uint32_t*nw=aa(c->a,nc*4);memcpy(nw,c->w,c->n*4);c->w=nw;c->c=nc;}c->w[c->n++]=w;}

/* Codegen: emit prologue + body + epilogue */
static void cg_func(CB *c,BI *bi,int32_t rv,int32_t frame){
    e(c,_spp(FP,LR,SP,-16,1));e(c,_ai(FP,SP,16,1));
    if(frame>0)e(c,_si(SP,SP,frame,1));
    /* Body: emit ops */
    for(int32_t i=0;i<bi->on;i++){
        switch(bi->ok[i]){
        case OP_LDC: e(c,_mz(R0,bi->o0[i],0)); break;
        case OP_BINOP:
            /* Load lhs to R0, rhs to R1, apply op, result in R0 */
            e(c,_mz(R0,bi->o0[i],0));
            e(c,_mz(R1,bi->o1[i],0));
            switch(bi->ot[i]){
            case BIN_ADD: e(c,_add(R0,R0,R1,0)); break;
            case BIN_SUB: e(c,_sub(R0,R0,R1,0)); break;
            case BIN_MUL: e(c,_mul(R0,R0,R1,0)); break;
            case BIN_DIV: e(c,_sdiv(R0,R0,R1,0)); break;
            }
            break;
        case OP_CMP:
            e(c,_mz(R0,bi->o0[i],0));
            e(c,_ci(R0,bi->o1[i],0));
            break;
        }
    }
    /* Store result */
    e(c,_sti(R0,SP,0,0));
    /* Terms - for now just return */
    if(frame>0)e(c,_ai(SP,SP,frame,1));
    e(c,_lpp(FP,LR,SP,16,1));
    e(c,_rt());
}

/* ================================================================
 * Output: write .s + cc (direct Mach-O via macho_direct.h for final)
 * ================================================================ */
static int out_cc(const char *path,CB *c){
    char sp[512];snprintf(sp,sizeof(sp),"%s.s",path);
    FILE *f=fopen(sp,"w");if(!f)return 1;
    fprintf(f,".global _main\n.align 2\n_main:\n");
    for(int32_t i=0;i<c->n;i++)fprintf(f,"  .long %u\n",c->w[i]);
    fclose(f);
    char cmd[1024];snprintf(cmd,sizeof(cmd),"cc -arch arm64 -o %s %s 2>&1",path,sp);
    return system(cmd);
}

/* ================================================================
 * Main
 * ================================================================ */
int main(int argc,char **argv){
    const char *out=argc>1?argv[1]:"/tmp/cc_out";
    const char *src=argc>2?argv[2]:NULL;
    Ar *a=mmap(0,sizeof(Ar),3,0x1002,-1,0);a->h=a->c=0;a->t=0;
    ST *st=st_new(a);
    BI *bi=bn(a);
    int32_t rv=42;

    if(src){
        S sf=sf_open(src);
        if(sf.n>0){
            P p={sf,0,a,st};
            while(p.pos<sf.n){
                p_ws(&p);if(p.pos>=sf.n)break;
                S kw=p_peek(&p);
                if(seq(kw,"type")){p_type(&p);}
                else if(seq(kw,"fn")){
                    BI *fbi=bn(a);p_fn(&p,fbi);
                    /* extract return value from first function */
                    if(st->n==1)for(int32_t i=0;i<fbi->on;i++)if(fbi->ok[i]==OP_LDC){rv=fbi->o0[i];break;}
                }
                else{p_line(&p);}
            }
            munmap((void*)sf.p,sf.n);
        }
    }

    /* Build main function IR */
    int32_t sl=bs(bi,SLOT_I32,4);
    bo(bi,OP_LDC,sl,rv,0);
    bt(bi,TM_RET,0,sl,0,-1,-1,-1);
    bb(bi,0,bi->on,bi->tn-1);

    CB *c=cn(a,256);
    cg_func(c,bi,rv,16);
    int rc=out_cc(out,c);
    printf("cheng_cold: %s  src=%s  fns=%d  types=%d  ops=%d  code=%dw  arena=%zuKB\n",
           rc?"FAIL":"OK",src?src:"(demo)",st->n,st->tn,bi->on,c->n,a->t/1024);
    munmap(a,sizeof(Ar));
    return rc;
}
