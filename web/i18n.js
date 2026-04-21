// Minimal i18n.
//
// Source of truth for the active language (priority order):
//   1. localStorage 'lang' — explicit choice from the footer switcher
//   2. navigator.language  — browser preference
//   3. 'en' fallback
//
// All translatable text comes from /static/locales/<lang>.json. Page title,
// labels, headers — anything that should change with language — uses a
// `data-i18n="..."` attribute and is rewritten when the dict loads.
// Operator customises wording by editing the locale files / index.html
// directly (no server-side config layer).
//
// Exposes window.I18N = { lang, t(key), setLang(lang) }.

(function () {
    const STORAGE_KEY = 'lang';
    const SUPPORTED   = ['ru', 'en'];

    function detect() {
        const saved = localStorage.getItem(STORAGE_KEY);
        if (saved && SUPPORTED.includes(saved)) return saved;
        const nav = (navigator.language || 'en').toLowerCase();
        return nav.startsWith('ru') ? 'ru' : 'en';
    }

    let dict = {};
    let lang = detect();

    async function load(l) {
        try {
            const r = await fetch('/static/locales/' + l + '.json', { cache: 'no-store' });
            if (r.ok) return await r.json();
        } catch (_) {}
        return {};
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
        apply();
        window.dispatchEvent(new CustomEvent('langchange', { detail: { lang } }));
    }

    document.addEventListener('click', (e) => {
        const btn = e.target.closest('.lang-btn');
        if (btn && btn.dataset.setLang) setLang(btn.dataset.setLang);
    });

    (async () => {
        dict = await load(lang);
        apply();
        window.dispatchEvent(new CustomEvent('langready', { detail: { lang } }));
    })();

    window.I18N = {
        get lang() { return lang; },
        t, setLang,
    };
})();
