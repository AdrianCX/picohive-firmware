// Ambience only. Nothing here touches the socket or any field.
(function () {
    var root = document.documentElement;
    // Motion is optional. The narration is not: it is text, so it runs either
    // way and only its fade is dropped (see the reduced-motion block above).
    var calm = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

    // What the blackout says, from the page's text (lost:).
    var LOST = PAGE_LOST;

    function rnd(a, b) { return a + Math.random() * (b - a); }

    // The plate comes apart over three and a half seconds and puts itself
    // back exactly as it was.
    function tear() {
        root.classList.add("gl");
        setTimeout(function () { root.classList.remove("gl"); }, 3400);
    }

    // Whiteout: everything goes pristine, holds a beat too long, then returns
    // through a tear, so it is never quite the same page that comes back.
    function whiteout() {
        var span = document.querySelector("#blackout span");
        if (span) span.textContent = LOST[Math.floor(Math.random() * LOST.length)];
        root.classList.add("void");
        setTimeout(function () {
            root.classList.remove("void");
            setTimeout(tear, 1500);
        }, rnd(3200, 5200));
    }

    function schedule() {
        setTimeout(function () {
            if (!document.hidden && root.classList.contains("t-xeno")) {
                Math.random() < 0.2 ? whiteout() : tear();
            }
            schedule();
        }, rnd(19000, 44000));
    }
    if (!calm) setTimeout(schedule, 6000);

    // One line, then a long silence, then another line. Each skin narrates in
    // its own voice, from the page's text (narration:); a skin with no lines
    // simply never opens the box.
    var NARRATION = PAGE_NARRATION;
    var box = document.getElementById("whisper");
    var holdTimer = null, dipTimer = null;

    // Lines come out shuffled, not in the order they are written. The bag is
    // refilled only once it is empty, so every line gets its turn before any
    // of them comes round a second time, and a fresh bag never opens with the
    // line that closed the last one.
    var bag = [], bagTheme = null, lastLine = null;

    function nextLine(theme, lines) {
        if (theme !== bagTheme || !bag.length) {
            bagTheme = theme;
            bag = lines.slice();
            for (var i = bag.length - 1; i > 0; i--) {   // Fisher-Yates
                var j = Math.floor(Math.random() * (i + 1));
                var swap = bag[i]; bag[i] = bag[j]; bag[j] = swap;
            }
            if (bag.length > 1 && bag[bag.length - 1] === lastLine) {
                bag.push(bag.shift());                   // no back-to-back repeat
            }
        }
        lastLine = bag.pop();
        return lastLine;
    }

    // The line stays up. It only leaves for the moment it takes to put the
    // next one in its place: hold, dip out, swap, come straight back.
    function whisper() {
        clearTimeout(holdTimer);
        clearTimeout(dipTimer);
        var lines = NARRATION[currentTheme];
        if (!box || !lines) {                    // this skin does not narrate
            holdTimer = setTimeout(whisper, 8000);
            return;
        }
        box.textContent = nextLine(currentTheme, lines);
        box.classList.add("on");
        holdTimer = setTimeout(function () {
            box.classList.remove("on");
            dipTimer = setTimeout(whisper, 700);
        }, rnd(12000, 20000));
    }

    // So a skin change can put its own voice up straight away instead of
    // leaving the corner empty until the next turn of the cycle.
    window.__whisperNext = whisper;

    // The first line arrives a few seconds in; after that it is always there.
    setTimeout(whisper, 2500);
})();
