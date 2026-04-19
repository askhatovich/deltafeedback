// Minimal i18n.
//
// Source of truth for the active language (priority order):
//   1. localStorage 'lang' — explicit choice from the footer switcher
//   2. navigator.language  — browser preference
//   3. 'en' fallback
//
// On top of the static locale files, the server can ship per-locale overrides
// for site-level fields (currently `title`) via /api/site. We fetch that once
// and merge it into the active dict on every (re)apply, so admin's config
// values take precedence over the locale defaults.
//
// Exposes window.I18N = { lang, t(key), setLang(lang) }. setLang() persists
// the choice and re-applies translations live (no reload).

(function () {
    const STORAGE_KEY = 'lang';
    const SUPPORTED   = ['ru', 'en'];

    function detect() {
        const saved = localStorage.getItem(STORAGE_KEY);
        if (saved && SUPPORTED.includes(saved)) return saved;
        const nav = (navigator.language || 'en').toLowerCase();
        return nav.startsWith('ru') ? 'ru' : 'en';
    }

    let dict    = {};
    let siteCfg = {};
    let lang    = detect();

    async function load(l) {
        try {
            const r = await fetch('/static/locales/' + l + '.json', { cache: 'no-store' });
            if (r.ok) return await r.json();
        } catch (_) {}
        return {};
    }

    async function loadSite() {
        try {
            const r = await fetch('/api/site', { cache: 'no-store' });
            if (r.ok) return await r.json();
        } catch (_) {}
        return {};
    }

    // Overlay site-config fields onto the locale dict for the current lang.
    // An empty server value falls through to the locale file default.
    function mergeSite() {
        const t = siteCfg.titles && siteCfg.titles[lang];
        if (t) dict.title = t;
    }

    function t(key) {
        return key.split('.').reduce((o, k) => (o && o[k] != null) ? o[k] : null, dict);
    }

    function apply() {
        document.documentElement.lang = lang;
        document.querySelectorAll('[data-i18n]').forEach(el => {
            const v = t(el.dataset.i18n);
            if (v != null) el.textContent = v;
        });
        document.querySelectorAll('.lang-btn').forEach(b => {
            b.classList.toggle('current', b.dataset.setLang === lang);
        });
    }

    async function setLang(next) {
        if (!SUPPORTED.includes(next) || next === lang) return;
        lang = next;
        localStorage.setItem(STORAGE_KEY, lang);
        dict = await load(lang);
        mergeSite();
        apply();
        window.dispatchEvent(new CustomEvent('langchange', { detail: { lang } }));
    }

    document.addEventListener('click', (e) => {
        const btn = e.target.closest('.lang-btn');
        if (btn && btn.dataset.setLang) setLang(btn.dataset.setLang);
    });

    (async () => {
        const [d, s] = await Promise.all([load(lang), loadSite()]);
        dict    = d;
        siteCfg = s;
        mergeSite();
        apply();
        window.dispatchEvent(new CustomEvent('langready', { detail: { lang } }));
    })();

    window.I18N = {
        get lang() { return lang; },
        t, setLang,
    };
})();
