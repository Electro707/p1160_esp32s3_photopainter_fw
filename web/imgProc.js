/**
 * Image processor file. Handles taking an image and creating a dithered output for the display
 *
 * Partially/Mostly ClaudeAI Generated with human oversight.
 */
// @ts-check

const TARG_W = 800, TARG_H = 480;

export const PALETTES = {
    waveshare: {
        label: 'Waveshare',
        colors: [
            [0,   0,   0  ],
            [255, 255, 255],
            [255, 255, 0  ],
            [255, 0,   0  ],
            [0,   0,   255],
            [0,   255, 0  ],
        ]
    },
    camera: {
        label: 'Camera-measured',
        colors: [
            [0,   0,   42 ],
            [198, 214, 224],
            [242, 220, 8  ],
            [176, 0,   0  ],
            [0,   64,  190],
            [0,   118, 101],
        ]
    }
};

const INDEX_TO_ENUM = [0, 1, 2, 3, 5, 6];

/**
 *
 * Takes an image file and builds a canvas out of it
 * @param {HTMLImageElement} img
 * @returns {HTMLCanvasElement}
 */
export function createCanvas(img){
    let imgW = img.width, imgH = img.height;

    const canvas = document.createElement('canvas');
    const ctx = canvas.getContext('2d');
    if (!ctx) throw new Error('no ctx');

    if (imgH > imgW) {
        canvas.width = imgH;
        canvas.height = imgW;
        ctx.translate(canvas.width / 2, canvas.height / 2);
        ctx.rotate(Math.PI / 2);
        ctx.drawImage(img, -imgW / 2, -imgH / 2);
        imgW = canvas.width;
        imgH = canvas.height;
    } else {
        canvas.width = imgW;
        canvas.height = imgH;
        ctx.drawImage(img, 0, 0);
    }

    // Scale
    const scale = Math.min(TARG_W / imgW, TARG_H / imgH);
    const resizedW = Math.floor(imgW * scale);
    const resizedH = Math.floor(imgH * scale);

    // Create target canvas (white background)
    const outCanvas = document.createElement('canvas');
    outCanvas.width = TARG_W;
    outCanvas.height = TARG_H;
    const outCtx = outCanvas.getContext('2d');
    if (!outCtx) throw new Error('no ctx');
    outCtx.fillStyle = 'white';
    outCtx.fillRect(0, 0, TARG_W, TARG_H);

    // Center the resized image
    const left = Math.floor((TARG_W - resizedW) / 2);
    const top = Math.floor((TARG_H - resizedH) / 2);
    outCtx.drawImage(canvas, 0, 0, imgW, imgH, left, top, resizedW, resizedH);

    // Rotate 180
    const rotCanvas = document.createElement('canvas');
    rotCanvas.width = TARG_W;
    rotCanvas.height = TARG_H;
    const rotCtx = rotCanvas.getContext('2d');
    if (!rotCtx) throw new Error('no ctx');
    rotCtx.translate(TARG_W / 2, TARG_H / 2);
    rotCtx.rotate(Math.PI);
    rotCtx.drawImage(outCanvas, -TARG_W / 2, -TARG_H / 2);

    return rotCanvas;
}
/**
 * Takes a canvas and dithers it, returning an int array
 *
 * @param {HTMLCanvasElement} img
 * @param {Array<Array<number>>} palette
 * @return {Uint8Array}
 */
export function ditherImage(img, palette){
    const ctx = img.getContext('2d');
    if (!ctx) throw new Error('no ctx');

    // Get pixel data
    const imageData = ctx.getImageData(0, 0, TARG_W, TARG_H);
    const numPixels = TARG_W * TARG_H;
    const pixels = new Float32Array(imageData.data)
    const quantized = new Uint8Array(numPixels);

    for (let y = 0; y < TARG_H; y++) {
        for (let x = 0; x < TARG_W; x++) {
            const idx = (y * TARG_W + x);
            const px = idx * 4;

            // Apply accumulated error
            const r = Math.min(255, Math.max(0, pixels[px]));
            const g = Math.min(255, Math.max(0, pixels[px + 1]));
            const b = Math.min(255, Math.max(0, pixels[px + 2]));

            // Find nearest palette color
            let bestIdx = 0, bestDist = Infinity;
            for (let p = 0; p < palette.length; p++) {
                const dr = r - palette[p][0];
                const dg = g - palette[p][1];
                const db = b - palette[p][2];
                const dist = dr*dr + dg*dg + db*db;
                if (dist < bestDist) { bestDist = dist; bestIdx = p; }
            }
            quantized[idx] = INDEX_TO_ENUM[bestIdx];

            // Distribute error (Floyd-Steinberg)
            const er = r - palette[bestIdx][0];
            const eg = g - palette[bestIdx][1];
            const eb = b - palette[bestIdx][2];

            const distribute = (/** @type {number} */ nx, /** @type {number} */ ny, /** @type {number} */ factor) => {
                if (nx < 0 || nx >= TARG_W || ny >= TARG_H) return;
                const ni = (ny * TARG_W + nx) * 4;
                pixels[ni    ] += er * factor;
                pixels[ni + 1] += eg * factor;
                pixels[ni + 2] += eb * factor;
            };

            distribute(x + 1, y,     7 / 16);
            distribute(x - 1, y + 1, 3 / 16);
            distribute(x,     y + 1, 5 / 16);
            distribute(x + 1, y + 1, 1 / 16);
        }
    }
    return quantized;
}

/**
 *
 * @param {Uint8Array} quant
 * @param {Array<Array<number>>} palette
 */
export function imageFromQuantized(quant, palette){
    const out = document.createElement('canvas');
    const numPixels = TARG_W * TARG_H;

    out.width = TARG_W; out.height = TARG_H;
    const outCtx = out.getContext('2d');
    if (!outCtx) throw new Error('no ctx');
    const outData = outCtx.createImageData(TARG_W, TARG_H);

    for (let i = 0; i < numPixels; i++) {
        const palIdx = INDEX_TO_ENUM.indexOf(quant[i]);
        const color = palette[palIdx];
        outData.data[i * 4    ] = color[0];
        outData.data[i * 4 + 1] = color[1];
        outData.data[i * 4 + 2] = color[2];
        outData.data[i * 4 + 3] = 255;
    }
    outCtx.putImageData(outData, 0, 0);
    return out;
}


/**
 * Takes a quantized image (per e-ink index) and returns it packed as expected by the firmware
 * @param {Uint8Array} quant
 * @return {Uint8Array}
 */
export function ditheredImgToBytes(quant){
    // Pack two nibbles per byte
    const packed = new Uint8Array(Math.floor(quant.length / 2));
    for (let i = 0; i < packed.length; i++) {
        // on the display, MSB is pixel 0
        packed[i] = (quant[i*2] & 0x0F) << 4;
        packed[i] |= quant[(i*2) + 1] & 0x0F;
    }
    return packed;
}

/**
 *
 * @param {Uint8Array} imgDat
 * @param {Array<Array<number>>} palette
 */
export function imageFromBytes(imgDat, palette){
    const out = document.createElement('canvas');
    const numPixels = TARG_W * TARG_H;

    out.width = TARG_W; out.height = TARG_H;
    const outCtx = out.getContext('2d');
    if (!outCtx) throw new Error('no ctx');
    const outData = outCtx.createImageData(TARG_W, TARG_H);

    for (let i = 0; i < numPixels; i++) {
        let pixel = imgDat[Math.floor(i / 2)];
        if(i % 2 === 0){pixel >>= 4};
        pixel &= 0xF;
        const palIdx = INDEX_TO_ENUM.indexOf(pixel);
        const color = palette[palIdx];
        outData.data[i * 4    ] = color[0];
        outData.data[i * 4 + 1] = color[1];
        outData.data[i * 4 + 2] = color[2];
        outData.data[i * 4 + 3] = 255;
    }
    outCtx.putImageData(outData, 0, 0);
    return out;
}

/**
 * @param {File} file
 * @returns {Promise<HTMLImageElement>}
 */
export function loadImage(file) {
    return new Promise((resolve, reject) => {
        const url = URL.createObjectURL(file);
        const img = new Image();
        img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
        img.onerror = reject;
        img.src = url;
    });
}