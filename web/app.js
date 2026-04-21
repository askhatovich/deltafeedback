// Application script — POW captcha, form submit, ticket polling.
//
// Endpoints (see server.cpp):
//   GET  /api/captcha          → { token, difficulty, expires_at }
//   POST /api/feedback         → { code, ticket_id?, read_token? }
//   GET  /api/tickets/<id>     → { id, status, messages: [...] } (Bearer token)
//   POST /api/tickets/<id>     → { code }                         (Bearer token)
//
// The current locale is taken from window.I18N.lang and sent automatically;
// no language input is exposed in the form.

(function () {
    const STORAGE_KEY = 'feedback_ticket';
    const POLL_INTERVAL_MS = 5000;

    const form    = document.getElementById('form');
    const ticket  = document.getElementById('ticket');
    const welcome = document.getElementById('welcome');
    const statusP = form.querySelector('.status');
    const ta      = form.querySelector('textarea[name=message]');
    const counter = form.querySelector('.counter .used');
    const newBtn  = ticket.querySelector('.new-ticket');

    ta.addEventListener('input', () => { counter.textContent = ta.value.length; });

    let pollTimer = null;
    let stored    = readStored();
    let currentTicket = null;  // last polled state, for re-render on lang change
    let lastMsgCount  = -1;    // -1 = no previous render; reset on new ticket view
    let lastStatus    = '';    // tracked alongside count for the 304 short-circuit

    if (stored && stored.id && stored.token) {
        showTicketView(stored);
    } else {
        showForm();
    }

    newBtn.addEventListener('click', () => {
        clearStored();
        stopPoll();
        ticket.hidden = true;
        showForm();
    });

    form.addEventListener('submit', onSubmit);

    // Re-render dynamic strings (status badge, etc.) whenever the i18n
    // dict becomes available — both on the initial async load (`langready`,
    // wins the race against the first poll) and on a manual switch
    // (`langchange`).
    function refreshI18n() {
        if (currentTicket) renderTicket(currentTicket, stored);
        if (!form.hidden)  loadWelcome();
    }
    window.addEventListener('langready',  refreshI18n);
    window.addEventListener('langchange', refreshI18n);

    function showForm() {
        document.body.classList.remove('ticket-view');
        form.hidden = false;
        form.dataset.startedAt = String(Date.now());
        statusP.textContent = '';
        // Force-clear the message body — browsers cache textarea contents
        // across reloads even with autocomplete="off". Name is intentionally
        // left alone so the browser's name autofill works.
        ta.value = '';
        counter.textContent = '0';
        loadWelcome();
    }

    // Fetches /static/welcome.<lang>.html and injects it into the welcome
    // section. Empty body or missing file → block stays hidden. Trusted
    // content (admin's own file on their server) → innerHTML is fine.
    async function loadWelcome() {
        const lang = (window.I18N && window.I18N.lang) || 'en';
        try {
            const r = await fetch('/static/welcome.' + lang + '.html', { cache: 'no-store' });
            if (!r.ok) { welcome.hidden = true; welcome.innerHTML = ''; return; }
            const html = (await r.text()).trim();
            if (!html) { welcome.hidden = true; welcome.innerHTML = ''; return; }
            welcome.innerHTML = html;
            welcome.hidden = false;
        } catch {
            welcome.hidden = true;
            welcome.innerHTML = '';
        }
    }

    async function onSubmit(e) {
        e.preventDefault();
        statusP.textContent = '';
        statusP.classList.remove('error');

        const fd = new FormData(form);
        const startedAt = parseInt(form.dataset.startedAt || '0', 10);
        const fillTimeMs = Date.now() - startedAt;

        const submitBtn = form.querySelector('button[type=submit]');
        submitBtn.disabled = true;
        statusP.textContent = tr('errors.computing') || '…';

        try {
            const cap = await fetchJSON('/api/captcha');
            const nonce = SHA256.findNonce(cap.token, cap.difficulty);
            if (nonce === null) throw new Error('nonce search exhausted');

            const body = {
                pow_token: cap.token,
                pow_nonce: nonce,
                name: fd.get('name') || '',
                message: fd.get('message') || '',
                locale: (window.I18N && window.I18N.lang) || 'en',
                honeypot: fd.get('website') || '',
                fill_time_ms: fillTimeMs,
            };
            const r = await postJSON('/api/feedback', body);
            if (!r.ok) {
                showError(r.body.code);
                return;
            }
            stored = { id: r.body.ticket_id, token: r.body.read_token };
            writeStored(stored);
            form.hidden = true;
            statusP.textContent = '';
            showTicketView(stored);
        } catch (err) {
            showError('network');
            console.error(err);
        } finally {
            submitBtn.disabled = false;
        }
    }

    function showTicketView(t) {
        lastMsgCount = -1;  // fresh view → first render will auto-scroll
        lastStatus   = '';  // and the very first poll won't short-circuit
        welcome.hidden = true;  // welcome belongs to the form view only
        ticket.hidden = false;
        document.body.classList.add('ticket-view');
        ticket.querySelector('.ticket-id').textContent = t.id;
        startPoll(t);
    }

    function startPoll(t) {
        stopPoll();
        const tick = () => poll(t).catch(err => console.warn(err));
        tick();
        pollTimer = setInterval(tick, POLL_INTERVAL_MS);
    }

    function stopPoll() {
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
    }

    async function poll(t) {
        // Pass the count/status we already have so the server can answer
        // 304 Not Modified instead of resending the same payload. Skipped
        // on the very first poll (lastMsgCount === -1) so we always get a
        // full response to render initially.
        let url = '/api/tickets/' + encodeURIComponent(t.id);
        if (lastMsgCount >= 0 && lastStatus) {
            url += '?have_count=' + lastMsgCount + '&have_status=' + encodeURIComponent(lastStatus);
        }
        const resp = await fetch(url, {
            headers: { 'Authorization': 'Bearer ' + t.token },
            cache: 'no-store',
        });
        if (resp.status === 304) return;  // nothing changed
        if (resp.status === 404) {
            clearStored();
            stopPoll();
            ticket.hidden = true;
            showForm();
            return;
        }
        if (!resp.ok) return;
        currentTicket = await resp.json();
        renderTicket(currentTicket, t);
    }

    function renderTicket(data, t) {
        const badge = ticket.querySelector('.ticket-status');
        badge.textContent = trStatus(data.status);
        badge.className = 'ticket-status badge ' + data.status;

        const list = ticket.querySelector('.messages');
        list.innerHTML = '';
        const fmt = makeTimeFormatter();
        for (const m of data.messages) {
            const li = document.createElement('li');
            li.className = m.sender;

            const body = document.createElement('span');
            body.className = 'msg-body';
            body.textContent = m.body;
            li.appendChild(body);

            const time = document.createElement('time');
            time.className = 'msg-time';
            const d = new Date(m.created_at * 1000);
            time.dateTime = d.toISOString();
            time.textContent = fmt.format(d);
            li.appendChild(time);

            list.appendChild(li);
        }
        // Auto-scroll only when something actually changed:
        //   - first render after page load / new ticket (lastMsgCount === -1)
        //   - new message appeared since last render
        // On language re-renders the count is unchanged, so we leave the
        // user's scroll position alone. (Idle polls don't reach here at all
        // — the server short-circuits with 304.)
        if (data.messages.length > lastMsgCount) {
            requestAnimationFrame(() => { list.scrollTop = list.scrollHeight; });
        }
        lastMsgCount = data.messages.length;
        lastStatus   = data.status;

        const replyForm = ticket.querySelector('#reply-form');
        if (data.status === 'awaiting_visitor') {
            replyForm.hidden = false;
            replyForm.onsubmit = (e) => onReplySubmit(e, t);
            const rta = replyForm.querySelector('textarea[name=body]');
            const rused = replyForm.querySelector('.reply-used');
            rta.oninput = () => { rused.textContent = rta.value.length; };
        } else {
            replyForm.hidden = true;
        }

        if (data.status === 'closed') {
            stopPoll();
            newBtn.hidden = false;
        } else {
            newBtn.hidden = true;
        }
    }

    async function onReplySubmit(e, t) {
        e.preventDefault();
        const replyForm = e.target;
        const ta = replyForm.querySelector('textarea[name=body]');
        const btn = replyForm.querySelector('button');
        btn.disabled = true;
        try {
            const cap = await fetchJSON('/api/captcha');
            const nonce = SHA256.findNonce(cap.token, cap.difficulty);
            const r = await postJSON('/api/tickets/' + encodeURIComponent(t.id), {
                pow_token: cap.token,
                pow_nonce: nonce,
                body: ta.value,
            }, { 'Authorization': 'Bearer ' + t.token });
            if (!r.ok) {
                alert(tr('errors.' + r.body.code) || r.body.code);
                return;
            }
            ta.value = '';
            replyForm.querySelector('.reply-used').textContent = '0';
            poll(t);
        } finally {
            btn.disabled = false;
        }
    }

    function showError(code) {
        statusP.textContent = tr('errors.' + code) || ('error: ' + code);
        statusP.classList.add('error');
    }

    function tr(key) { return (window.I18N && window.I18N.t) ? window.I18N.t(key) : null; }
    function trStatus(s) { return tr('ticket.' + s) || s; }

    // Browser-local time, locale-aware: short month + day + hour:minute.
    function makeTimeFormatter() {
        const lang = (window.I18N && window.I18N.lang) || navigator.language || 'en';
        return new Intl.DateTimeFormat(lang, {
            month: 'short', day: 'numeric',
            hour: '2-digit', minute: '2-digit',
        });
    }

    async function fetchJSON(url) {
        const r = await fetch(url, { cache: 'no-store' });
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
    }

    async function postJSON(url, body, headers) {
        const r = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...(headers || {}) },
            body: JSON.stringify(body),
        });
        const text = await r.text();
        let parsed = {};
        try { parsed = text ? JSON.parse(text) : {}; } catch { parsed = { code: 'bad_json' }; }
        return { ok: r.ok, status: r.status, body: parsed };
    }

    function readStored() {
        try { return JSON.parse(localStorage.getItem(STORAGE_KEY)); } catch { return null; }
    }
    function writeStored(v) { localStorage.setItem(STORAGE_KEY, JSON.stringify(v)); }
    function clearStored()  { localStorage.removeItem(STORAGE_KEY); }
})();
