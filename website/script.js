// Include the translations script in index.html first, then use it here.
// Assuming translations object is available globally or imported if we use modules.
// For this simple setup, we assume translations.js is loaded before script.js in index.html.
// We need to update index.html to include translations.js first.

document.addEventListener('DOMContentLoaded', () => {
    const langSelector = document.getElementById('lang-selector');

    // Default language or saved preference
    let currentLang = localStorage.getItem('cheng-lang') || 'zh-CN';

    // Update selector value
    if (langSelector) {
        langSelector.value = currentLang;
    }

    // Function to update texts
    function updateLanguage(lang) {
        if (!translations[lang]) return;

        // Save preference
        localStorage.setItem('cheng-lang', lang);

        // Update document title
        if (translations[lang]["title"]) {
            document.title = translations[lang]["title"];
        }

        // Update RTL
        // User requested unified layout (LTR) for all languages including Arabic.
        document.documentElement.dir = 'ltr';
        document.documentElement.lang = lang;

        // Update all data-i18n elements
        document.querySelectorAll('[data-i18n]').forEach(el => {
            const key = el.getAttribute('data-i18n');
            if (translations[lang][key]) {
                el.innerHTML = translations[lang][key];
            }
        });

        // Update placeholders
        document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
            const key = el.getAttribute('data-i18n-placeholder');
            if (translations[lang][key]) {
                el.placeholder = translations[lang][key];
            }
        });
    }

    // Initialize language
    updateLanguage(currentLang);

    // Event listener
    if (langSelector) {
        langSelector.addEventListener('change', (e) => {
            updateLanguage(e.target.value);
        });
    }

    // Smooth Scrolling for anchor links
    document.querySelectorAll('a[href^="#"]').forEach(anchor => {
        anchor.addEventListener('click', function (e) {
            e.preventDefault();
            const target = document.querySelector(this.getAttribute('href'));
            if (target) {
                target.scrollIntoView({
                    behavior: 'smooth'
                });
            }
        });
    });

    // Intersection Observer for fade-in animations
    const observerOptions = {
        threshold: 0.1,
        rootMargin: "0px 0px -50px 0px"
    };

    const observer = new IntersectionObserver((entries) => {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                entry.target.classList.add('visible');
                observer.unobserve(entry.target); // Only animate once
            }
        });
    }, observerOptions);

    const animatedElements = document.querySelectorAll('.fade-in');
    animatedElements.forEach(el => observer.observe(el));

    // --- Navigation Logic (SPA Behavior) ---
    const heroSection = document.querySelector('.hero-section');
    const packagesSection = document.getElementById('packages');
    const navLinks = document.querySelectorAll('.nav-links a');
    const logoLink = document.querySelector('.logo'); // Assuming logo click returns home

    function showSection(sectionId) {
        // Reset Views
        if (heroSection) heroSection.style.display = 'none';
        if (packagesSection) packagesSection.style.display = 'none';

        // Toggle specific view
        if (sectionId === 'packages') {
            if (packagesSection) {
                packagesSection.style.display = 'block';
                // Trigger animation reflow if needed, or just let fade-in work
                packagesSection.classList.add('visible');
            }
        } else {
            // Default to Home/Hero
            if (heroSection) {
                heroSection.style.display = 'flex'; // Hero uses flex
            }
        }
        window.scrollTo(0, 0);
    }

    // Initialize View (Default to Home unless hash is #packages)
    if (window.location.hash === '#packages') {
        showSection('packages');
    } else {
        showSection('home');
    }

    // Handle Navigation Clicks
    navLinks.forEach(link => {
        link.addEventListener('click', (e) => {
            const href = link.getAttribute('href');

            // External links (Github)
            if (href.startsWith('http')) return;

            e.preventDefault();

            if (href === '#packages') {
                showSection('packages');
                history.pushState(null, null, '#packages');
            } else if (href === '#features' || href === '#code') {
                // Features and Code are inside Hero
                showSection('home');
                history.pushState(null, null, 'index.html'); // Clear hash or set to #home
            } else if (href === '#docs') {
                // For now docs also just stays on home or external, but let's assume external or separate page in future. 
                // Current requirement doesn't specify Docs page, keep as Home implementation or alert.
                // Actually, let (href === '#docs') go to home for now or leave default behavior if it was a real page.
                // Given the context, let's treat it as Home for now to avoid broken link.
                showSection('home');
            }
        });
    });

    // Handle Logo Click
    if (logoLink) {
        logoLink.addEventListener('click', () => {
            showSection('home');
            history.pushState(null, null, 'index.html');
        });
    }

    // Code Tab Switching Logic (Inner Tabs)
    const tabs = document.querySelectorAll('.tab-btn');
    const contents = document.querySelectorAll('.code-content');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            // Remove active class from all tabs and contents
            tabs.forEach(t => t.classList.remove('active'));
            contents.forEach(c => c.classList.remove('active'));

            // Add active class to clicked tab
            tab.classList.add('active');

            // Show corresponding content
            const targetId = tab.getAttribute('data-tab');
            const targetContent = document.getElementById(targetId);
            if (targetContent) {
                targetContent.classList.add('active');
            }
        });
    });
    // --- Package Manager Logic (LibP2P Simulation) ---
    const packages = [
        { name: "cheng-http", version: "0.4.1", desc: "Backend-focused HTTP/1.1 server implementation with zero GC allocations.", tags: ["net", "backend"], downloads: "12k", verified: true },
        { name: "cheng-ui", version: "0.8.0", desc: "Hardware accelerated immediate mode GUI for embedded and desktop.", tags: ["gui", "graphics"], downloads: "8.5k", verified: true },
        { name: "cheng-libp2p", version: "0.2.3", desc: "Native bindings for peer-to-peer networking stack.", tags: ["p2p", "net"], downloads: "5.1k", verified: true },
        { name: "cheng-crypto", version: "1.0.0", desc: "Constant-time cryptographic primitives verified by formal proofs.", tags: ["security", "math"], downloads: "15k", verified: true },
        { name: "cheng-math-fixed", version: "0.1.0", desc: "Fixed-point arithmetic library for deterministic simulation workloads.", tags: ["math", "deterministic"], downloads: "3.2k", verified: false },
        { name: "cheng-db-core", version: "0.5.2", desc: "Embedded key-value store optimized for NVMe.", tags: ["db", "storage"], downloads: "4k", verified: true },
        { name: "cheng-game-engine", version: "0.3.0", desc: "2D/3D ECS game engine boilerplate.", tags: ["game", "engine"], downloads: "2.1k", verified: false },
        { name: "cheng-gpio", version: "0.9.1", desc: "Zero-latency GPIO control for RISC-V and ARM.", tags: ["iot", "embedded"], downloads: "6.7k", verified: true },
    ];

    const pkgGrid = document.getElementById('pkg-list');
    const pkgSearch = document.getElementById('pkg-search');

    function renderPackages(filter = "") {
        if (!pkgGrid) return;
        pkgGrid.innerHTML = "";

        const term = filter.toLowerCase();
        const filtered = packages.filter(p =>
            p.name.toLowerCase().includes(term) ||
            p.desc.toLowerCase().includes(term) ||
            p.tags.some(t => t.includes(term))
        );

        filtered.forEach(pkg => {
            const card = document.createElement('div');
            card.className = 'pkg-card fade-in visible'; // Force visible since we are dynamically adding

            const verifiedIcon = pkg.verified ? '<span class="pkg-verified" title="Verified Publisher">✓</span>' : '';
            const tagsHtml = pkg.tags.map(t => `<span class="pkg-tag">#${t}</span>`).join('');

            card.innerHTML = `
                <div class="pkg-header">
                    <div>
                        <div class="pkg-name">${pkg.name}</div>
                        <span class="pkg-version">v${pkg.version}</span>
                    </div>
                    ${verifiedIcon}
                </div>
                <div class="pkg-tags">${tagsHtml}</div>
                <p class="pkg-desc">${pkg.desc}</p>
                <div class="pkg-meta">
                    <span>⬇ ${pkg.downloads}</span>
                    <span>LibP2P Swarm</span>
                </div>
            `;
            pkgGrid.appendChild(card);
        });

        if (filtered.length === 0) {
            pkgGrid.innerHTML = `<div style="grid-column: 1/-1; text-align: center; color: #666; padding: 2rem;">No packages found in swarm for "${filter}"</div>`;
        }
    }

    // Initial render
    renderPackages();

    // Live search listener
    if (pkgSearch) {
        pkgSearch.addEventListener('input', (e) => {
            renderPackages(e.target.value);
        });
    }
    // --- Fractal Background Animation (WebGL) ---
    const canvas = document.getElementById('fractal-bg');
    if (canvas) {
        const gl = canvas.getContext('webgl');
        if (gl) {
            // Vertex Shader: Pass through
            const vsSource = `
                attribute vec4 aVertexPosition;
                void main() {
                    gl_Position = aVertexPosition;
                }
            `;

            // Fragment Shader: Animated Julia Set with Domain Coloring feel
            const fsSource = `
                precision mediump float;
                uniform vec2 uResolution;
                uniform float uTime;

                // Color palette based on Cheng brand (Blue/Cyan/Dark)
                vec3 palette(float t) {
                    vec3 a = vec3(0.5, 0.5, 0.5);
                    vec3 b = vec3(0.5, 0.5, 0.5);
                    vec3 c = vec3(1.0, 1.0, 1.0);
                    vec3 d = vec3(0.30, 0.415, 0.6); // Blue-ish offset
                    return a + b * cos(6.28318 * (c * t + d));
                }

                void main() {
                    vec2 uv = (gl_FragCoord.xy * 2.0 - uResolution.xy) / min(uResolution.x, uResolution.y);
                    
                    // Zoom and pan
                    uv *= 1.5;
                    
                    // Animated parameters for Julia Set c = a + bi
                    float time = uTime * 0.15;
                    vec2 c = vec2(cos(time) * 0.7885 + 0.1, sin(time * 0.5) * 0.7885);

                    vec2 z = uv;
                    float iter = 0.0;
                    float maxIter = 80.0;
                    
                    for (float i = 0.0; i < 80.0; i++) {
                        // z = z^2 + c
                        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
                        
                        // Escape condition
                        if (dot(z, z) > 4.0) break;
                        iter++;
                    }

                    // Smooth coloring
                    float sn = iter - log2(log2(dot(z, z))) + 4.0;
                    float t = sn / maxIter;
                    
                    // Dark theme background
                    vec3 col = vec3(0.0);
                    if (iter < maxIter) {
                        col = palette(t * 1.5 + uTime * 0.1);
                        // Mix with dark background to not be too overwhelming
                        col = mix(vec3(0.04, 0.04, 0.07), col, 0.3); // High transparency feeling
                    } else {
                        // Interior
                        col = vec3(0.02, 0.02, 0.03);
                    }

                    // Vignette to fade edges to black (seamless blend)
                    float vig = 1.0 - length(uv * 0.5);
                    col *= smoothstep(0.0, 1.0, vig);

                    gl_FragColor = vec4(col, 1.0);
                }
            `;

            function createShader(gl, type, source) {
                const shader = gl.createShader(type);
                gl.shaderSource(shader, source);
                gl.compileShader(shader);
                if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
                    console.error('Shader compile error:', gl.getShaderInfoLog(shader));
                    gl.deleteShader(shader);
                    return null;
                }
                return shader;
            }

            const shaderProgram = gl.createProgram();
            gl.attachShader(shaderProgram, createShader(gl, gl.VERTEX_SHADER, vsSource));
            gl.attachShader(shaderProgram, createShader(gl, gl.FRAGMENT_SHADER, fsSource));
            gl.linkProgram(shaderProgram);

            if (!gl.getProgramParameter(shaderProgram, gl.LINK_STATUS)) {
                console.error('Program init failed:', gl.getProgramInfoLog(shaderProgram));
            }

            const positionBuffer = gl.createBuffer();
            gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
            const positions = [-1.0, 1.0, 1.0, 1.0, -1.0, -1.0, 1.0, -1.0];
            gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(positions), gl.STATIC_DRAW);

            const timerLoc = gl.getUniformLocation(shaderProgram, 'uTime');
            const resLoc = gl.getUniformLocation(shaderProgram, 'uResolution');
            const posLoc = gl.getAttribLocation(shaderProgram, 'aVertexPosition');

            function resize() {
                // Low-res for performance if needed, but modern GPUs handle full res fine
                // Use device pixel ratio for sharpness but limit to 2x
                const dpr = Math.min(window.devicePixelRatio, 2);
                canvas.width = window.innerWidth * dpr;
                canvas.height = window.innerHeight * dpr;
                gl.viewport(0, 0, canvas.width, canvas.height);
            }
            window.addEventListener('resize', resize);
            resize();

            let startTime = performance.now();
            function render() {
                const time = (performance.now() - startTime) * 0.001;

                gl.useProgram(shaderProgram);
                gl.enableVertexAttribArray(posLoc);
                gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
                gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

                gl.uniform1f(timerLoc, time);
                gl.uniform2f(resLoc, canvas.width, canvas.height);

                gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
                requestAnimationFrame(render);
            }
            requestAnimationFrame(render);
        }
    }
});
