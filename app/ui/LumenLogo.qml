import QtQuick

// The sun mark from the mockup toolbar (24-unit SVG drawn on a canvas).
Canvas {
    implicitWidth: 22
    implicitHeight: 22
    Accessible.ignored: true

    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        ctx.scale(width / 24, height / 24);
        ctx.strokeStyle = Theme.live.toString();
        ctx.lineWidth = 2;
        ctx.lineCap = "round";

        ctx.beginPath();
        ctx.arc(12, 12, 4, 0, 2 * Math.PI);
        ctx.stroke();

        const rays = [[12, 2, 12, 5], [12, 19, 12, 22], [2, 12, 5, 12], [19, 12, 22, 12],
                      [4.9, 4.9, 7, 7], [17, 17, 19.1, 19.1], [4.9, 19.1, 7, 17], [17, 7, 19.1, 4.9]];
        ctx.beginPath();
        for (const r of rays) {
            ctx.moveTo(r[0], r[1]);
            ctx.lineTo(r[2], r[3]);
        }
        ctx.stroke();
    }
}
