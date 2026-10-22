// --- Calendar --------------------------------------------------------------
// The board reads its calendar once an hour; Refresh and Bind read it now.
// Its "calendar" answer fills the Upcoming table: first line how the calendar
// is doing, then "now\t<board time>", then "<when>\t<command>" per command
// coming up, with "\tnot allowed" when the list would skip it.
// A line without a tab is a note. The board's time is the Date of its last
// fetch moved on by its own clock, so this is where the pico thinks it is.
var CALENDAR_POLL_MS = 2000;       // while a fetch is under way
var CALENDAR_POLL_TRIES = 15;
var CALENDAR_TICK_MS = 30000;      // while the tab is open, to keep the board time current
var calendarPolls = 0;

function calendarLinkUp() {
    return handler.ws && (handler.ws.readyState === WebSocket.OPEN);
}

function calendarShow() {
    if (calendarLinkUp()) handler.send("/rc/calendar", T("cfg.calUpcoming"), true);
}

function calendarRefresh() {
    if (handler.send("/rc/calendar/refresh", T("cfg.calRefresh"), true)) {
        calendarPolls = CALENDAR_POLL_TRIES;
        setTimeout(calendarShow, CALENDAR_POLL_MS);
    }
}

function calendarCell(row, text, attrs) {
    var td = document.createElement("td");
    td.textContent = text;
    for (var k in (attrs || {})) td.setAttribute(k, attrs[k]);
    row.appendChild(td);
    return td;
}

function showCalendarStatus(text) {
    var lines = text.split("\n");
    var state = lines.shift();
    var now = "\u2014";
    var events = [];
    var notes = [];

    lines.forEach((line) => {
        var f = line.split("\t");
        if (f[0] === "now" && f.length > 1) now = f[1];
        else if (f.length >= 2) events.push({ when: f[0], command: f[1], blocked: f[2] === "not allowed" });
        else if (line.trim()) notes.push(line);
    });

    document.getElementById("calendar_now").textContent = now;
    document.getElementById("calendar_state").textContent = state;

    // Titles come from whoever writes in the calendar: text only, never markup.
    var describe = (e) => e.command + (e.blocked ? " (" + T("cal.notAllowed") + ")" : "");

    var body = document.getElementById("calendar_events");
    while (body.firstChild) body.removeChild(body.firstChild);

    events.forEach((e) => {
        var row = document.createElement("tr");
        if (e.blocked) row.className = "cal-blocked";
        // "Mon 2026-10-05" and "11:21", so the line breaks between them.
        var when = calendarCell(row, "", { class: "cal-when" });
        var cut = e.when.lastIndexOf(" ");
        [e.when.substring(0, cut), e.when.substring(cut + 1)].forEach((part, i) => {
            var span = document.createElement("span");
            span.textContent = part;
            if (i) when.appendChild(document.createTextNode(" "));
            when.appendChild(span);
        });
        calendarCell(row, describe(e), { class: "cal-command" });
        body.appendChild(row);
    });

    // With nothing coming up the table still says why, across both columns.
    notes.forEach((note) => {
        var row = document.createElement("tr");
        calendarCell(row, note, { class: "hint", colspan: "2" });
        body.appendChild(row);
    });

    // Still fetching after a refresh: ask again shortly.
    if ((calendarPolls > 0) && /fetching now/.test(state)) {
        calendarPolls--;
        setTimeout(calendarShow, CALENDAR_POLL_MS);
    } else {
        calendarPolls = 0;
    }
}

setInterval(() => {
    // offsetParent is null while the panel, or the tab it sits in, is hidden.
    var panel = document.getElementById("sub_calendar");
    if (panel && (panel.offsetParent !== null) && !document.hidden && (calendarPolls === 0)) calendarShow();
}, CALENDAR_TICK_MS);


// Its answer goes in the Upcoming table, not a toast that is gone before it is
// read; opening the section, a load and a Bind all ask for it.
onMessage("/rc/calendar", showCalendarStatus);
onSubTabShown("sub_calendar", calendarShow);
onConfigLoaded(calendarShow);
onConfigSent(calendarRefresh);
