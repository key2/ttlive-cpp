// hybrid-fake-dom.js — pure stubs + deep auto-proxying safety net
// Uses hardcoded browser stubs (no replay log) but wraps browser API objects
// with safeProxy() so unknown property chains never return undefined.
// Native JS builtins (Math, Array, etc.) are NOT wrapped — they work fine as-is.

"use strict";

// ============================================================
// safeProxy — wraps any object so unknown properties return callable proxies
// ============================================================
function safeProxy(target, name) {
    if (target === null || target === undefined) target = function(){};
    if (typeof target !== 'object' && typeof target !== 'function') return target;

    return new Proxy(typeof target === 'function' ? target : function(){}, {
        get: function(_, prop) {
            if (prop === Symbol.toPrimitive) return function() { return ''; };
            if (prop === Symbol.iterator) return undefined;
            if (prop === 'then') return undefined; // prevent Promise detection
            if (prop === 'toString') return function() { return '[object Object]'; };
            if (prop === 'valueOf') return function() { return 0; };
            if (prop === 'length') return typeof target === 'function' ? target.length : (target.length || 0);
            if (prop === 'constructor') return target.constructor || Object;
            if (prop === 'prototype') return target.prototype || {};

            // Try real property first
            if (target && prop in target) {
                var val = target[prop];
                if (typeof val === 'object' && val !== null) return safeProxy(val, (name || '?') + '.' + String(prop));
                if (typeof val === 'function') return val; // keep real functions
                return val; // primitives pass through
            }

            // Unknown property — return a safe callable proxy
            return safeProxy(function(){}, (name || '?') + '.' + String(prop));
        },
        set: function(_, prop, value) {
            if (target && typeof target === 'object') target[prop] = value;
            return true;
        },
        apply: function(_, thisArg, args) {
            if (typeof target === 'function') {
                try { return target.apply(thisArg, args); } catch(e) { return safeProxy(function(){}); }
            }
            return safeProxy(function(){});
        },
        construct: function(_, args) {
            if (typeof target === 'function') {
                try { return new target(...args); } catch(e) { return safeProxy({}); }
            }
            return safeProxy({});
        },
        has: function(_, prop) { return true; },
        getOwnPropertyDescriptor: function(t, prop) {
            var desc = Object.getOwnPropertyDescriptor(t, prop);
            if (desc) return desc;
            if (target && typeof target === 'object' && prop in target) {
                return { configurable: true, enumerable: true, writable: true, value: target[prop] };
            }
            return undefined;
        },
        ownKeys: function(t) { return Reflect.ownKeys(t); }
    });
}

// ============================================================
// atob / btoa
// ============================================================
function atob(str) {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';
    let output = '';
    str = String(str).replace(/=+$/, '');
    for (let i = 0; i < str.length; i += 4) {
        let a = chars.indexOf(str[i]), b = chars.indexOf(str[i + 1]);
        let c = chars.indexOf(str[i + 2]), d = chars.indexOf(str[i + 3]);
        let bits = (a << 18) | (b << 12) | (c << 6) | d;
        output += String.fromCharCode((bits >> 16) & 0xFF);
        if (c !== 64) output += String.fromCharCode((bits >> 8) & 0xFF);
        if (d !== 64) output += String.fromCharCode(bits & 0xFF);
    }
    return output;
}

function btoa(str) {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    let output = '';
    for (let i = 0; i < str.length; i += 3) {
        let a = str.charCodeAt(i), b = str.charCodeAt(i + 1), c = str.charCodeAt(i + 2);
        let bits = (a << 16) | ((b || 0) << 8) | (c || 0);
        output += chars[(bits >> 18) & 63] + chars[(bits >> 12) & 63];
        output += (i + 1 < str.length) ? chars[(bits >> 6) & 63] : '=';
        output += (i + 2 < str.length) ? chars[bits & 63] : '=';
    }
    return output;
}

// ============================================================
// Cookie jar
// ============================================================
var __cookieJar = {};

function __getCookies() {
    var parts = [];
    for (var k in __cookieJar) {
        parts.push(k + '=' + __cookieJar[k]);
    }
    return parts.join('; ');
}

function __setCookie(str) {
    if (!str) return;
    var pair = str.split(';')[0];
    var idx = pair.indexOf('=');
    if (idx < 0) return;
    var key = pair.substring(0, idx).trim();
    var val = pair.substring(idx + 1).trim();
    if (key) __cookieJar[key] = val;
}

// Seed initial cookies
__setCookie('tiktok_webapp_theme_source=auto');
__setCookie('tiktok_webapp_theme=light');

// ============================================================
// FakeEvent / FakeEventTarget
// ============================================================
class FakeEvent {
    constructor(type, opts) {
        this.type = type;
        this.bubbles = (opts && opts.bubbles) || false;
        this.cancelable = (opts && opts.cancelable) || false;
        this.defaultPrevented = false;
        this.target = null;
        this.currentTarget = null;
        this.timeStamp = Date.now();
        this.isTrusted = false;
    }
    preventDefault() { this.defaultPrevented = true; }
    stopPropagation() {}
    stopImmediatePropagation() {}
}

class FakeEventTarget {
    constructor() { this._listeners = {}; }
    addEventListener(type, fn) { (this._listeners[type] = this._listeners[type] || []).push(fn); }
    removeEventListener(type, fn) { this._listeners[type] = (this._listeners[type] || []).filter(f => f !== fn); }
    dispatchEvent(event) { (this._listeners[event.type] || []).forEach(fn => { try { fn(event); } catch (e) {} }); return true; }
}

// ============================================================
// FakeStorage
// ============================================================
class FakeStorage {
    constructor() { this._data = {}; }
    getItem(k) { return k in this._data ? this._data[k] : null; }
    setItem(k, v) { this._data[k] = String(v); }
    removeItem(k) { delete this._data[k]; }
    clear() { this._data = {}; }
    key(i) { var keys = Object.keys(this._data); return keys[i] || null; }
    get length() { return Object.keys(this._data).length; }
}

// ============================================================
// TextEncoder / TextDecoder
// ============================================================
class FakeTextEncoder {
    constructor() { this.encoding = 'utf-8'; }
    encode(str) {
        str = str || '';
        var arr = [];
        for (var i = 0; i < str.length; i++) {
            var c = str.charCodeAt(i);
            if (c < 0x80) { arr.push(c); }
            else if (c < 0x800) { arr.push(0xC0 | (c >> 6), 0x80 | (c & 0x3F)); }
            else if (c < 0xD800 || c >= 0xE000) { arr.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F)); }
            else { i++; c = 0x10000 + (((c & 0x3FF) << 10) | (str.charCodeAt(i) & 0x3FF)); arr.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 0x3F), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F)); }
        }
        return new Uint8Array(arr);
    }
}

class FakeTextDecoder {
    constructor(label) { this.encoding = label || 'utf-8'; }
    decode(buf) {
        if (!buf) return '';
        var bytes = new Uint8Array(buf.buffer || buf);
        var out = '', i = 0;
        while (i < bytes.length) {
            var b = bytes[i++];
            if (b < 0x80) out += String.fromCharCode(b);
            else if (b < 0xE0) out += String.fromCharCode(((b & 0x1F) << 6) | (bytes[i++] & 0x3F));
            else if (b < 0xF0) { var b2 = bytes[i++]; out += String.fromCharCode(((b & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (bytes[i++] & 0x3F)); }
            else { var b2 = bytes[i++], b3 = bytes[i++], cp = ((b & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (bytes[i++] & 0x3F); cp -= 0x10000; out += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF)); }
        }
        return out;
    }
}

// ============================================================
// FakeXMLHttpRequest
// ============================================================
class FakeXMLHttpRequest extends FakeEventTarget {
    constructor() {
        super();
        this.readyState = 0;
        this.response = '';
        this.responseText = '';
        this.responseType = '';
        this.responseURL = '';
        this.responseXML = null;
        this.status = 0;
        this.statusText = '';
        this.timeout = 0;
        this.withCredentials = false;
        this.upload = new FakeEventTarget();
        this._headers = {};
        this._responseHeaders = {};
        this._method = 'GET';
        this._url = '';
        this._async = true;
        this._sent = false;

        // Event handlers
        this.onreadystatechange = null;
        this.onload = null;
        this.onerror = null;
        this.onabort = null;
        this.ontimeout = null;
        this.onprogress = null;
        this.onloadstart = null;
        this.onloadend = null;
    }

    open(method, url, async, user, password) {
        if (typeof globalThis.__stderr !== 'undefined') {
            globalThis.__stderr.puts('[XHR-OPEN-RECEIVED] ' + method + ' ' + String(url).substring(0, 500) + '\n');
            if (String(url).includes('X-Bogus')) globalThis.__stderr.puts('[XHR-SIGNED!] X-Bogus found in URL!\n');
            if (String(url).includes('X-Gnarly')) globalThis.__stderr.puts('[XHR-SIGNED!] X-Gnarly found in URL!\n');
        }
        this._method = method;
        this._url = url;
        this._async = async !== false;
        this.readyState = 1;
        this._changeState();
    }

    setRequestHeader(name, value) {
        if (typeof globalThis.__stderr !== 'undefined') {
            globalThis.__stderr.puts('[XHR-HEADER] ' + name + ' = ' + String(value).substring(0, 200) + '\n');
        }
        this._headers[name.toLowerCase()] = value;
    }

    getResponseHeader(name) {
        return this._responseHeaders[name.toLowerCase()] || null;
    }

    getAllResponseHeaders() {
        return Object.entries(this._responseHeaders)
            .map(([k,v]) => k + ': ' + v)
            .join('\r\n');
    }

    send(body) {
        if (typeof globalThis.__stderr !== 'undefined') {
            globalThis.__stderr.puts('[XHR-SEND] ' + this._method + ' ' + this._url + '\n');
            if (body) globalThis.__stderr.puts('[XHR-BODY] ' + String(body).substring(0, 500) + '\n');
        }
        this._sent = true;
        this.readyState = 2;
        this._changeState();

        // Simulate async completion
        this.readyState = 4;
        this.status = 200;
        this.statusText = 'OK';
        this.responseText = '{}';
        this.response = '{}';
        this._changeState();

        // Fire events
        this._fireEvent('load');
        this._fireEvent('loadend');
    }

    abort() {
        this.readyState = 0;
        this._fireEvent('abort');
    }

    overrideMimeType(mime) {}

    _changeState() {
        if (this.onreadystatechange) {
            try { this.onreadystatechange(); } catch(e) {}
        }
        this._fireEvent('readystatechange');
    }

    _fireEvent(type) {
        var handler = this['on' + type];
        if (handler) { try { handler.call(this, {}); } catch(e) {} }
        (this._listeners[type] || []).forEach(fn => { try { fn.call(this, {}); } catch(e) {} });
    }
}

// Static constants
FakeXMLHttpRequest.UNSENT = 0;
FakeXMLHttpRequest.OPENED = 1;
FakeXMLHttpRequest.HEADERS_RECEIVED = 2;
FakeXMLHttpRequest.LOADING = 3;
FakeXMLHttpRequest.DONE = 4;
FakeXMLHttpRequest.prototype.UNSENT = 0;
FakeXMLHttpRequest.prototype.OPENED = 1;
FakeXMLHttpRequest.prototype.HEADERS_RECEIVED = 2;
FakeXMLHttpRequest.prototype.LOADING = 3;
FakeXMLHttpRequest.prototype.DONE = 4;

// ============================================================
// fakeFetch — must be a plain function, NOT a Proxy, so SDK can wrap it
// ============================================================
function fakeFetch(input, init) {
    var url = typeof input === 'string' ? input : (input && input.url) || '';
    var method = (init && init.method) || 'GET';
    var headers = (init && init.headers) || {};
    var body = (init && init.body) || null;
    if (typeof globalThis.__stderr !== 'undefined') {
        globalThis.__stderr.puts('[FETCH] ' + method + ' ' + url + '\n');
        if (body) globalThis.__stderr.puts('[FETCH-BODY] ' + String(body).substring(0, 500) + '\n');
        try { globalThis.__stderr.puts('[FETCH-HEADERS] ' + JSON.stringify(headers) + '\n'); } catch(e) {}
    }
    return Promise.resolve({
        ok: true, status: 200, statusText: 'OK',
        url: url,
        headers: new (globalThis.Headers || function(){})(),
        json: function() { return Promise.resolve({}); },
        text: function() { return Promise.resolve(''); },
        arrayBuffer: function() { return Promise.resolve(new ArrayBuffer(0)); },
        blob: function() { return Promise.resolve(new FakeBlob([])); },
        clone: function() { return this; }
    });
}

// ============================================================
// FakeCanvasRenderingContext2D
// ============================================================
class FakeCanvasRenderingContext2D {
    constructor(canvas) {
        this.canvas = canvas;
        this.fillStyle = '#000000'; this.strokeStyle = '#000000';
        this.globalAlpha = 1; this.lineWidth = 1;
        this.lineCap = 'butt'; this.lineJoin = 'miter';
        this.miterLimit = 10; this.shadowBlur = 0;
        this.shadowColor = 'rgba(0, 0, 0, 0)';
        this.shadowOffsetX = 0; this.shadowOffsetY = 0;
        this.font = '10px sans-serif'; this.textAlign = 'start';
        this.textBaseline = 'alphabetic'; this.globalCompositeOperation = 'source-over';
        this.imageSmoothingEnabled = true;
    }
    save() {} restore() {} scale() {} rotate() {} translate() {} transform() {} setTransform() {} resetTransform() {}
    createLinearGradient() { return { addColorStop() {} }; }
    createRadialGradient() { return { addColorStop() {} }; }
    createPattern() { return {}; }
    clearRect() {} fillRect() {} strokeRect() {} fillText() {} strokeText() {}
    measureText(text) { return { width: (text || '').length * 6, actualBoundingBoxAscent: 8, actualBoundingBoxDescent: 2, fontBoundingBoxAscent: 10, fontBoundingBoxDescent: 2 }; }
    beginPath() {} closePath() {} moveTo() {} lineTo() {} bezierCurveTo() {} quadraticCurveTo() {} arc() {} arcTo() {} ellipse() {} rect() {} fill() {} stroke() {} clip() {} isPointInPath() { return false; }
    drawImage() {} createImageData(w, h) { return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }; }
    getImageData(x, y, w, h) { return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }; }
    putImageData() {} setLineDash() {} getLineDash() { return []; }
}

// ============================================================
// WebGL Context
// ============================================================
function createFakeWebGLContext() {
    var params = {
        7936: 'WebKit', 7937: 'WebKit WebGL',
        7938: 'WebGL 1.0 (OpenGL ES 2.0 Chromium)',
        35724: 'WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)',
        37445: 'Google Inc. (Apple)',
        37446: 'ANGLE (Apple, ANGLE Metal Renderer: Apple M1 Pro, Unspecified Version)',
        3379: 16384, 34076: 16384, 34024: 16384, 36347: 1024,
        36348: 4096, 36349: 1024, 34921: 16, 35660: 16, 35661: 32, 34930: 16,
        7939: 'WebGL 1.0', 3410: 8, 3411: 8, 3412: 8, 3413: 8, 3414: 24, 3415: 8,
        33901: new Float32Array([1, 1]), 33902: new Float32Array([1, 1]), 34047: 16,
    };
    var extensions = [
        'ANGLE_instanced_arrays', 'EXT_blend_minmax', 'EXT_color_buffer_half_float',
        'EXT_disjoint_timer_query', 'EXT_float_blend', 'EXT_frag_depth',
        'EXT_shader_texture_lod', 'EXT_texture_compression_bptc',
        'EXT_texture_compression_rgtc', 'EXT_texture_filter_anisotropic',
        'EXT_sRGB', 'KHR_parallel_shader_compile', 'OES_element_index_uint',
        'OES_fbo_render_mipmap', 'OES_standard_derivatives',
        'OES_texture_float', 'OES_texture_float_linear',
        'OES_texture_half_float', 'OES_texture_half_float_linear',
        'OES_vertex_array_object', 'WEBGL_color_buffer_float',
        'WEBGL_compressed_texture_s3tc', 'WEBGL_compressed_texture_s3tc_srgb',
        'WEBGL_debug_renderer_info', 'WEBGL_debug_shaders',
        'WEBGL_depth_texture', 'WEBGL_draw_buffers',
        'WEBGL_lose_context', 'WEBGL_multi_draw'
    ];
    return {
        getParameter: function (pname) { return params[pname] !== undefined ? params[pname] : null; },
        getSupportedExtensions: function () { return extensions; },
        getExtension: function (name) {
            if (name === 'WEBGL_debug_renderer_info') return { UNMASKED_VENDOR_WEBGL: 0x9245, UNMASKED_RENDERER_WEBGL: 0x9246 };
            if (name === 'EXT_texture_filter_anisotropic') return { MAX_TEXTURE_MAX_ANISOTROPY_EXT: 0x84FF, TEXTURE_MAX_ANISOTROPY_EXT: 0x84FE };
            if (extensions.indexOf(name) >= 0) return {};
            return null;
        },
        createBuffer: function () { return {}; }, createShader: function () { return {}; }, createProgram: function () { return {}; },
        shaderSource: function () {}, compileShader: function () {}, attachShader: function () {}, linkProgram: function () {},
        getShaderParameter: function () { return true; }, getProgramParameter: function () { return true; },
        useProgram: function () {}, deleteShader: function () {}, deleteProgram: function () {}, deleteBuffer: function () {},
        bindBuffer: function () {}, bufferData: function () {}, enableVertexAttribArray: function () {}, vertexAttribPointer: function () {},
        drawArrays: function () {}, drawElements: function () {}, viewport: function () {}, clear: function () {}, clearColor: function () {},
        enable: function () {}, disable: function () {}, blendFunc: function () {}, depthFunc: function () {},
        getAttribLocation: function () { return 0; }, getUniformLocation: function () { return {}; },
        uniform1f: function () {}, uniform2f: function () {}, uniform3f: function () {}, uniform4f: function () {},
        uniform1i: function () {}, uniformMatrix4fv: function () {},
        createTexture: function () { return {}; }, bindTexture: function () {}, texImage2D: function () {},
        texParameteri: function () {}, generateMipmap: function () {}, deleteTexture: function () {},
        createFramebuffer: function () { return {}; }, bindFramebuffer: function () {}, framebufferTexture2D: function () {},
        checkFramebufferStatus: function () { return 0x8CD5; },
        readPixels: function (x, y, w, h, fmt, type, pixels) { if (pixels) for (var i = 0; i < pixels.length; i++) pixels[i] = 0; },
        getContextAttributes: function () { return { alpha: true, antialias: true, depth: true, failIfMajorPerformanceCaveat: false, powerPreference: 'default', premultipliedAlpha: true, preserveDrawingBuffer: false, stencil: false }; },
        isContextLost: function () { return false; },
        canvas: null, drawingBufferWidth: 300, drawingBufferHeight: 150,
        getShaderInfoLog: function () { return ''; }, getProgramInfoLog: function () { return ''; },
        createRenderbuffer: function () { return {}; }, bindRenderbuffer: function () {},
        renderbufferStorage: function () {}, framebufferRenderbuffer: function () {},
        pixelStorei: function () {}, activeTexture: function () {}, scissor: function () {},
        stencilFunc: function () {}, stencilOp: function () {}, stencilMask: function () {},
        colorMask: function () {}, depthMask: function () {}, lineWidth: function () {},
        flush: function () {}, finish: function () {}, clearDepth: function () {}, clearStencil: function () {},
        blendFuncSeparate: function () {}, blendEquation: function () {}, blendEquationSeparate: function () {},
        blendColor: function () {}, vertexAttrib1f: function () {}, vertexAttrib2f: function () {},
        vertexAttrib3f: function () {}, vertexAttrib4f: function () {}, disableVertexAttribArray: function () {},
        uniform2i: function () {}, uniform3i: function () {}, uniform4i: function () {},
        uniform1fv: function () {}, uniform2fv: function () {}, uniform3fv: function () {}, uniform4fv: function () {},
        uniform1iv: function () {}, uniform2iv: function () {}, uniform3iv: function () {}, uniform4iv: function () {},
        uniformMatrix2fv: function () {}, uniformMatrix3fv: function () {},
        isEnabled: function () { return false; }, getError: function () { return 0; },
    };
}

// ============================================================
// Observers
// ============================================================
class FakeMutationObserver {
    constructor(cb) { this._cb = cb; }
    observe() {} disconnect() {} takeRecords() { return []; }
}
class FakeIntersectionObserver {
    constructor(cb) { this._cb = cb; this.root = null; this.rootMargin = '0px'; this.thresholds = [0]; }
    observe() {} unobserve() {} disconnect() {} takeRecords() { return []; }
}
class FakeResizeObserver {
    constructor(cb) { this._cb = cb; }
    observe() {} unobserve() {} disconnect() {}
}
class FakePerformanceObserver {
    constructor(cb) { this._cb = cb; }
    observe() {} disconnect() {} takeRecords() { return []; }
}
FakePerformanceObserver.supportedEntryTypes = ['mark', 'measure', 'navigation', 'resource', 'longtask', 'paint', 'largest-contentful-paint', 'first-input', 'layout-shift'];

// ============================================================
// Performance
// ============================================================
var perfStart = Date.now();
var fakePerformance = {
    now: function () { return Date.now() - perfStart; },
    timeOrigin: perfStart,
    timing: {
        navigationStart: perfStart, unloadEventStart: 0, unloadEventEnd: 0,
        redirectStart: 0, redirectEnd: 0, fetchStart: perfStart,
        domainLookupStart: perfStart, domainLookupEnd: perfStart,
        connectStart: perfStart, connectEnd: perfStart,
        secureConnectionStart: perfStart, requestStart: perfStart,
        responseStart: perfStart + 50, responseEnd: perfStart + 100,
        domLoading: perfStart + 100, domInteractive: perfStart + 200,
        domContentLoadedEventStart: perfStart + 200, domContentLoadedEventEnd: perfStart + 200,
        domComplete: perfStart + 300, loadEventStart: perfStart + 300, loadEventEnd: perfStart + 300
    },
    navigation: { type: 0, redirectCount: 0 },
    getEntries: function () { return []; },
    getEntriesByType: function () { return []; },
    getEntriesByName: function () { return []; },
    mark: function () {}, measure: function () {},
    clearMarks: function () {}, clearMeasures: function () {},
    clearResourceTimings: function () {}, setResourceTimingBufferSize: function () {},
    addEventListener: function () {}, removeEventListener: function () {},
    toJSON: function () { return { timing: this.timing, navigation: this.navigation, timeOrigin: this.timeOrigin }; }
};

// ============================================================
// Crypto
// ============================================================
var fakeCrypto = {
    getRandomValues: function (arr) {
        for (var i = 0; i < arr.length; i++) {
            if (arr.BYTES_PER_ELEMENT === 1) arr[i] = (Math.random() * 256) | 0;
            else if (arr.BYTES_PER_ELEMENT === 2) arr[i] = (Math.random() * 65536) | 0;
            else arr[i] = (Math.random() * 4294967296) | 0;
        }
        return arr;
    },
    randomUUID: function () {
        return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) {
            var r = (Math.random() * 16) | 0;
            return (c === 'x' ? r : (r & 0x3) | 0x8).toString(16);
        });
    },
    subtle: {
        digest: function () { return Promise.resolve(new ArrayBuffer(32)); },
        encrypt: function () { return Promise.resolve(new ArrayBuffer(0)); },
        decrypt: function () { return Promise.resolve(new ArrayBuffer(0)); },
        sign: function () { return Promise.resolve(new ArrayBuffer(0)); },
        verify: function () { return Promise.resolve(false); },
        generateKey: function () { return Promise.resolve({}); },
        importKey: function () { return Promise.resolve({}); },
        exportKey: function () { return Promise.resolve(new ArrayBuffer(0)); },
        deriveBits: function () { return Promise.resolve(new ArrayBuffer(0)); },
        deriveKey: function () { return Promise.resolve({}); }
    }
};

// ============================================================
// FakeElement + FakeCanvasElement
// ============================================================
var fakeDocument; // forward declaration

class FakeElement {
    constructor(tagName) {
        this.tagName = tagName.toUpperCase();
        this.nodeName = this.tagName;
        this.nodeType = 1;
        this.children = []; this.childNodes = [];
        this.attributes = {}; this._attrs = [];
        this.style = new Proxy({}, { get: function () { return ''; }, set: function () { return true; } });
        this.classList = { add: function () {}, remove: function () {}, contains: function () { return false; }, toggle: function () {} };
        this.dataset = {};
        this.innerHTML = ''; this.outerHTML = '';
        this.textContent = ''; this.innerText = '';
        this.parentNode = null; this.parentElement = null;
        this.nextSibling = null; this.previousSibling = null;
        this.firstChild = null; this.lastChild = null;
        this.offsetWidth = 100; this.offsetHeight = 100;
        this.offsetLeft = 0; this.offsetTop = 0;
        this.clientWidth = 100; this.clientHeight = 100;
        this.scrollWidth = 100; this.scrollHeight = 100;
        this.scrollLeft = 0; this.scrollTop = 0;
        this.id = ''; this.className = '';
        this._listeners = {};
        this.src = ''; this.href = '';
        this.type = ''; this.rel = '';
        this.value = ''; this.checked = false;
        this.disabled = false; this.readOnly = false;
        this.name = '';
    }
    setAttribute(k, v) { this.attributes[k] = String(v); this._attrs.push({ name: k, value: String(v) }); if (k === 'id') this.id = v; if (k === 'class') this.className = v; }
    getAttribute(k) { return this.attributes[k] !== undefined ? this.attributes[k] : null; }
    hasAttribute(k) { return k in this.attributes; }
    removeAttribute(k) { delete this.attributes[k]; this._attrs = this._attrs.filter(function (a) { return a.name !== k; }); }
    appendChild(child) { this.children.push(child); this.childNodes.push(child); if (child && typeof child === 'object') { child.parentNode = this; child.parentElement = this; } return child; }
    removeChild(child) { this.children = this.children.filter(function (c) { return c !== child; }); this.childNodes = this.childNodes.filter(function (c) { return c !== child; }); return child; }
    insertBefore(newChild, ref) { return this.appendChild(newChild); }
    replaceChild(newChild, oldChild) { this.removeChild(oldChild); return this.appendChild(newChild); }
    cloneNode(deep) { return new FakeElement(this.tagName); }
    querySelector(sel) { return null; }
    querySelectorAll(sel) { return []; }
    getElementsByTagName(tag) { return []; }
    getElementsByClassName(cls) { return []; }
    addEventListener(type, fn, opts) { (this._listeners[type] = this._listeners[type] || []).push(fn); }
    removeEventListener(type, fn) { this._listeners[type] = (this._listeners[type] || []).filter(function (f) { return f !== fn; }); }
    dispatchEvent(event) { (this._listeners[event.type] || []).forEach(function (fn) { try { fn(event); } catch (e) {} }); return true; }
    getBoundingClientRect() { return { top: 0, left: 0, right: 100, bottom: 100, width: 100, height: 100, x: 0, y: 0 }; }
    getClientRects() { return [this.getBoundingClientRect()]; }
    focus() {} blur() {} click() {}
    matches(sel) { return false; }
    closest(sel) { return null; }
    contains(node) { return false; }
    get ownerDocument() { return fakeDocument; }
    toString() { return '[object HTMLElement]'; }
    get nextElementSibling() { return null; }
    get previousElementSibling() { return null; }
    get firstElementChild() { return this.children[0] || null; }
    get lastElementChild() { return this.children[this.children.length - 1] || null; }
    get childElementCount() { return this.children.length; }
    remove() { if (this.parentNode) this.parentNode.removeChild(this); }
    hasChildNodes() { return this.childNodes.length > 0; }
    normalize() {}
    isEqualNode() { return false; }
    isSameNode(n) { return this === n; }
    compareDocumentPosition() { return 0; }
}

class FakeCanvasElement extends FakeElement {
    constructor() { super('canvas'); this.width = 300; this.height = 150; }
    getContext(type) {
        if (type === '2d') return new FakeCanvasRenderingContext2D(this);
        if (type === 'webgl' || type === 'experimental-webgl') return createFakeWebGLContext();
        return null;
    }
    toDataURL(type) { return 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=='; }
    toBlob(cb, type) { if (cb) cb(new Blob([''], { type: type || 'image/png' })); }
}

// ============================================================
// Navigator
// ============================================================
var fakeNavigator = {
    userAgent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36',
    platform: 'MacIntel',
    language: 'en-US',
    languages: ['en-US', 'en'],
    hardwareConcurrency: 8,
    maxTouchPoints: 0,
    cookieEnabled: true,
    webdriver: false,
    vendor: 'Google Inc.',
    appVersion: '5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36',
    appName: 'Netscape',
    product: 'Gecko',
    productSub: '20030107',
    onLine: true,
    deviceMemory: 8,
    connection: { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false },
    plugins: {
        length: 5,
        0: { name: 'PDF Viewer', filename: 'internal-pdf-viewer', description: 'Portable Document Format', length: 1 },
        1: { name: 'Chrome PDF Viewer', filename: 'internal-pdf-viewer', description: '', length: 1 },
        2: { name: 'Chromium PDF Viewer', filename: 'internal-pdf-viewer', description: '', length: 1 },
        3: { name: 'Microsoft Edge PDF Viewer', filename: 'internal-pdf-viewer', description: '', length: 1 },
        4: { name: 'WebKit built-in PDF', filename: 'internal-pdf-viewer', description: '', length: 1 },
        item: function (i) { return this[i]; },
        namedItem: function (n) { for (var i = 0; i < this.length; i++) if (this[i].name === n) return this[i]; return null; },
        refresh: function () {}
    },
    mimeTypes: {
        length: 2,
        0: { type: 'application/pdf', suffixes: 'pdf', description: 'Portable Document Format' },
        1: { type: 'text/pdf', suffixes: 'pdf', description: '' },
        item: function (i) { return this[i]; },
        namedItem: function (n) { for (var i = 0; i < this.length; i++) if (this[i].type === n) return this[i]; return null; }
    },
    doNotTrack: null,
    getBattery: function () { return Promise.resolve({ charging: true, chargingTime: 0, dischargingTime: Infinity, level: 1 }); },
    getGamepads: function () { return []; },
    javaEnabled: function () { return false; },
    sendBeacon: function () { return true; },
    vibrate: function () { return true; },
    mediaDevices: { enumerateDevices: function () { return Promise.resolve([]); } },
    permissions: { query: function () { return Promise.resolve({ state: 'prompt' }); } },
    clipboard: { readText: function () { return Promise.resolve(''); }, writeText: function () { return Promise.resolve(); } },
    serviceWorker: { ready: Promise.resolve(null), register: function () { return Promise.resolve(); }, getRegistrations: function () { return Promise.resolve([]); } },
    storage: { estimate: function () { return Promise.resolve({ quota: 1073741824, usage: 0 }); } },
    locks: { request: function (n, cb) { return cb({ mode: 'exclusive', name: n }); } },
    credentials: { get: function () { return Promise.resolve(null); }, create: function () { return Promise.resolve(null); } },
    geolocation: { getCurrentPosition: function (s, e) { if (e) e({ code: 1, message: 'denied' }); } },
    scheduling: { isInputPending: function () { return false; } },
    userAgentData: { brands: [{ brand: 'Chromium', version: '120' }, { brand: 'Not_A Brand', version: '8' }, { brand: 'Google Chrome', version: '120' }], mobile: false, platform: 'macOS', getHighEntropyValues: function () { return Promise.resolve({ platform: 'macOS', platformVersion: '13.0.0', architecture: 'arm', model: '', uaFullVersion: '120.0.0.0' }); } },
    webkitGetUserMedia: function () {},
    mediaCapabilities: { decodingInfo: function () { return Promise.resolve({ supported: true, smooth: true, powerEfficient: true }); } }
};

// ============================================================
// Screen
// ============================================================
var fakeScreen = {
    width: 1920, height: 1080,
    availWidth: 1920, availHeight: 1055,
    colorDepth: 24, pixelDepth: 24,
    orientation: { type: 'landscape-primary', angle: 0, addEventListener: function () {}, removeEventListener: function () {} }
};

// ============================================================
// Location
// ============================================================
var fakeLocation = {
    href: 'https://www.tiktok.com/',
    hostname: 'www.tiktok.com',
    pathname: '/',
    protocol: 'https:',
    origin: 'https://www.tiktok.com',
    search: '',
    hash: '',
    port: '',
    host: 'www.tiktok.com',
    assign: function () {},
    replace: function () {},
    reload: function () {},
    toString: function () { return this.href; }
};

// ============================================================
// Document
// ============================================================
var bodyElement = new FakeElement('body');
var headElement = new FakeElement('head');
var htmlElement = new FakeElement('html');
htmlElement.appendChild(headElement);
htmlElement.appendChild(bodyElement);

fakeDocument = {
    createElement: function (tag) {
        if (tag === 'canvas') return new FakeCanvasElement();
        var el = new FakeElement(tag);
        if (tag === 'script' || tag === 'link' || tag === 'img') {
            Object.defineProperty(el, 'src', {
                get: function () { return this._src || ''; },
                set: function (v) { this._src = v; if (this.onload) { try { this.onload(); } catch (e) {} } },
                configurable: true
            });
        }
        return el;
    },
    createElementNS: function (ns, tag) { return new FakeElement(tag); },
    createTextNode: function (text) { return { nodeType: 3, textContent: text, nodeName: '#text' }; },
    createDocumentFragment: function () {
        var frag = new FakeElement('fragment');
        frag.nodeType = 11;
        frag.nodeName = '#document-fragment';
        return frag;
    },
    createComment: function (text) { return { nodeType: 8, textContent: text, nodeName: '#comment' }; },
    createEvent: function (type) { return new FakeEvent(type); },
    getElementById: function (id) { return null; },
    querySelector: function (sel) {
        if (sel === 'head') return headElement;
        if (sel === 'body') return bodyElement;
        if (sel === 'html') return htmlElement;
        return null;
    },
    querySelectorAll: function (sel) { return []; },
    getElementsByTagName: function (tag) {
        tag = tag.toLowerCase();
        if (tag === 'body') return [bodyElement];
        if (tag === 'head') return [headElement];
        if (tag === 'html') return [htmlElement];
        if (tag === 'script') return [];
        return [];
    },
    getElementsByClassName: function (cls) { return []; },
    getElementsByName: function (name) { return []; },
    body: bodyElement,
    head: headElement,
    documentElement: htmlElement,
    readyState: 'complete',
    characterSet: 'UTF-8',
    charset: 'UTF-8',
    inputEncoding: 'UTF-8',
    title: 'TikTok',
    domain: 'www.tiktok.com',
    referrer: '',
    URL: 'https://www.tiktok.com/',
    documentURI: 'https://www.tiktok.com/',
    location: fakeLocation,
    compatMode: 'CSS1Compat',
    contentType: 'text/html',
    nodeType: 9,
    nodeName: '#document',
    defaultView: null, // set later
    hidden: false,
    visibilityState: 'visible',
    fullscreenEnabled: false,
    fullscreenElement: null,
    _listeners: {},
    hasFocus: function () { return true; },
    addEventListener: function (type, fn, opts) { (this._listeners[type] = this._listeners[type] || []).push(fn); },
    removeEventListener: function (type, fn) { this._listeners[type] = (this._listeners[type] || []).filter(function (f) { return f !== fn; }); },
    dispatchEvent: function (event) { (this._listeners[event.type] || []).forEach(function (fn) { try { fn(event); } catch (e) {} }); return true; },
    adoptNode: function (node) { return node; },
    importNode: function (node) { return node; },
    write: function () {},
    writeln: function () {},
    open: function () {},
    close: function () {},
    execCommand: function () { return false; },
    getSelection: function () { return { rangeCount: 0, toString: function () { return ''; } }; },
    createRange: function () { return { setStart: function () {}, setEnd: function () {}, collapse: function () {}, cloneRange: function () { return this; }, getBoundingClientRect: function () { return { top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0 }; }, getClientRects: function () { return []; } }; },
    createTreeWalker: function () { return { nextNode: function () { return null; }, currentNode: null }; },
    createNodeIterator: function () { return { nextNode: function () { return null; } }; },
    implementation: { hasFeature: function () { return true; }, createHTMLDocument: function (title) { return fakeDocument; } },
    childNodes: [htmlElement],
    children: [htmlElement],
    firstChild: htmlElement,
    lastChild: htmlElement,
    firstElementChild: htmlElement,
    lastElementChild: htmlElement,
    childElementCount: 1,
};

Object.defineProperty(fakeDocument, 'cookie', {
    get: function () { return __getCookies(); },
    set: function (v) { __setCookie(v); },
    configurable: true
});

// ============================================================
// Blob & URL
// ============================================================
class FakeBlob {
    constructor(parts, opts) {
        this.size = 0;
        this.type = (opts && opts.type) || '';
        if (parts) {
            for (var i = 0; i < parts.length; i++) {
                var p = parts[i];
                if (typeof p === 'string') this.size += p.length;
                else if (p && p.byteLength !== undefined) this.size += p.byteLength;
                else if (p && p.size !== undefined) this.size += p.size;
            }
        }
    }
    slice() { return new FakeBlob([], { type: this.type }); }
    text() { return Promise.resolve(''); }
    arrayBuffer() { return Promise.resolve(new ArrayBuffer(0)); }
}

var blobCounter = 0;
var FakeURL = {
    createObjectURL: function (blob) { return 'blob:https://www.tiktok.com/' + (blobCounter++); },
    revokeObjectURL: function () {}
};

// ============================================================
// Image constructor
// ============================================================
function FakeImage(w, h) {
    var el = new FakeElement('img');
    el.width = w || 0;
    el.height = h || 0;
    el.naturalWidth = w || 0;
    el.naturalHeight = h || 0;
    el.complete = true;
    Object.defineProperty(el, 'src', {
        get: function () { return this._src || ''; },
        set: function (v) { this._src = v; if (this.onload) { try { this.onload(); } catch (e) {} } },
        configurable: true
    });
    return el;
}

// ============================================================
// Timer queue — proper deferred queue without depth limits
// ============================================================
var __timerQueue = [];
var __timerId = 0;
var __drainingTimers = false;

// ============================================================
// Window listeners
// ============================================================
var _windowListeners = {};

// ============================================================
// Computed style proxy
// ============================================================
function getComputedStyle(el) {
    return new Proxy({}, {
        get: function (t, p) {
            if (p === 'getPropertyValue') return function () { return ''; };
            if (p === 'length') return 0;
            if (p === 'cssText') return '';
            return '';
        }
    });
}

// ============================================================
// Native builtins set — returned directly, NOT wrapped
// ============================================================
var nativeBuiltins = {
    Math: Math, Date: Date, Array: Array, Object: Object, String: String,
    Number: Number, Boolean: Boolean, RegExp: RegExp, JSON: JSON,
    parseInt: parseInt, parseFloat: parseFloat, isNaN: isNaN, isFinite: isFinite,
    encodeURIComponent: encodeURIComponent, decodeURIComponent: decodeURIComponent,
    encodeURI: encodeURI, decodeURI: decodeURI, escape: escape, unescape: unescape,
    ArrayBuffer: ArrayBuffer, Uint8Array: Uint8Array, Uint16Array: Uint16Array,
    Uint32Array: Uint32Array, Int8Array: Int8Array, Int16Array: Int16Array,
    Int32Array: Int32Array, Float32Array: Float32Array, Float64Array: Float64Array,
    Uint8ClampedArray: Uint8ClampedArray, DataView: DataView,
    Map: Map, Set: Set, WeakMap: WeakMap, WeakSet: WeakSet,
    Promise: Promise, Symbol: Symbol, Proxy: Proxy, Reflect: Reflect,
    Error: Error, TypeError: TypeError, RangeError: RangeError, SyntaxError: SyntaxError,
    URIError: URIError, EvalError: EvalError, ReferenceError: ReferenceError,
    NaN: NaN, Infinity: Infinity, undefined: undefined, null: null,
    eval: eval,
};

// ============================================================
// Browser API stubs map
// ============================================================
var _localStorage = new FakeStorage();
var _sessionStorage = new FakeStorage();

var browserAPIs = {
    navigator: fakeNavigator,
    document: fakeDocument,
    screen: fakeScreen,
    location: fakeLocation,
    history: { length: 2, state: null, pushState: function () {}, replaceState: function () {}, back: function () {}, forward: function () {}, go: function () {} },
    localStorage: _localStorage,
    sessionStorage: _sessionStorage,
    performance: fakePerformance,
    crypto: fakeCrypto,
    console: (function() {
        var base = (typeof globalThis.console !== 'undefined') ? globalThis.console : {};
        var noop = function () {};
        var methods = ['log','warn','error','info','debug','trace','dir','table','time','timeEnd','group','groupEnd','assert','clear','count'];
        for (var i = 0; i < methods.length; i++) {
            if (typeof base[methods[i]] !== 'function') base[methods[i]] = noop;
        }
        return base;
    })(),
    atob: atob,
    btoa: btoa,
    fetch: fakeFetch,
    XMLHttpRequest: FakeXMLHttpRequest,
    TextEncoder: FakeTextEncoder,
    TextDecoder: FakeTextDecoder,
    Blob: FakeBlob,
    File: FakeBlob,
    FileReader: function () {
        return { readAsDataURL: function () {}, readAsArrayBuffer: function () {}, readAsText: function () {}, readAsBinaryString: function () {}, abort: function () {}, onload: null, onerror: null, onabort: null, result: null, readyState: 0, addEventListener: function () {}, removeEventListener: function () {} };
    },
    URL: FakeURL,
    URLSearchParams: (typeof globalThis.URLSearchParams !== 'undefined') ? globalThis.URLSearchParams : function (init) {
        var params = {};
        this.get = function (k) { return params[k] || null; };
        this.set = function (k, v) { params[k] = v; };
        this.has = function (k) { return k in params; };
        this.delete = function (k) { delete params[k]; };
        this.toString = function () { var s = []; for (var k in params) s.push(encodeURIComponent(k) + '=' + encodeURIComponent(params[k])); return s.join('&'); };
        this.forEach = function (cb) { for (var k in params) cb(params[k], k); };
        this.entries = function () { var e = []; for (var k in params) e.push([k, params[k]]); return e[Symbol.iterator] ? e : { next: function () { return { done: true }; } }; };
    },
    Headers: function (init) {
        var h = {};
        this.get = function (k) { return h[k.toLowerCase()] || null; };
        this.set = function (k, v) { h[k.toLowerCase()] = v; };
        this.has = function (k) { return k.toLowerCase() in h; };
        this.delete = function (k) { delete h[k.toLowerCase()]; };
        this.forEach = function (cb) { for (var k in h) cb(h[k], k); };
        this.append = function (k, v) { h[k.toLowerCase()] = v; };
        if (init && typeof init === 'object') { for (var k in init) h[k.toLowerCase()] = init[k]; }
    },
    Request: function (url, opts) { this.url = url; this.method = (opts && opts.method) || 'GET'; this.headers = {}; },
    Response: function (body, opts) { this.ok = true; this.status = 200; this.statusText = 'OK'; this.body = body; },
    AbortController: function () {
        this.signal = { aborted: false, addEventListener: function () {}, removeEventListener: function () {} };
        this.abort = function () { this.signal.aborted = true; };
    },
    AbortSignal: { abort: function () { return { aborted: true }; } },
    Event: FakeEvent,
    CustomEvent: function (type, opts) { var e = new FakeEvent(type, opts); e.detail = (opts && opts.detail) || null; return e; },
    MessageEvent: function (type, opts) { var e = new FakeEvent(type, opts); e.data = (opts && opts.data) || null; return e; },
    MutationObserver: FakeMutationObserver,
    IntersectionObserver: FakeIntersectionObserver,
    ResizeObserver: FakeResizeObserver,
    PerformanceObserver: FakePerformanceObserver,
    Image: FakeImage,
    HTMLCanvasElement: FakeCanvasElement,
    HTMLElement: FakeElement,
    HTMLDivElement: FakeElement,
    HTMLImageElement: FakeElement,
    HTMLScriptElement: FakeElement,
    HTMLStyleElement: FakeElement,
    HTMLInputElement: FakeElement,
    HTMLFormElement: FakeElement,
    HTMLAnchorElement: FakeElement,
    HTMLIFrameElement: FakeElement,
    Element: FakeElement,
    Node: FakeElement,
    NodeList: Array,
    HTMLCollection: Array,
    DOMParser: function () {
        this.parseFromString = function (str, type) { return fakeDocument; };
    },
    CSSStyleDeclaration: function () { return {}; },
    Storage: FakeStorage,
    WebSocket: function (url) {
        this.url = url; this.readyState = 1;
        this.send = function () {}; this.close = function () {};
        this.addEventListener = function () {}; this.removeEventListener = function () {};
        this.onopen = null; this.onmessage = null; this.onerror = null; this.onclose = null;
    },
    Worker: function (url) {
        this.postMessage = function () {}; this.terminate = function () {};
        this.addEventListener = function () {}; this.removeEventListener = function () {};
    },
    MessageChannel: function () {
        this.port1 = { postMessage: function () {}, addEventListener: function () {}, removeEventListener: function () {}, start: function () {}, close: function () {} };
        this.port2 = { postMessage: function () {}, addEventListener: function () {}, removeEventListener: function () {}, start: function () {}, close: function () {} };
    },
    MessagePort: function () {
        this.postMessage = function () {}; this.start = function () {}; this.close = function () {};
        this.addEventListener = function () {}; this.removeEventListener = function () {};
    },
    BroadcastChannel: function (name) {
        this.name = name; this.postMessage = function () {}; this.close = function () {};
        this.addEventListener = function () {}; this.removeEventListener = function () {};
    },
    WebGLRenderingContext: function () { return createFakeWebGLContext(); },
    PerformanceMark: function (name) { this.name = name; this.entryType = 'mark'; this.startTime = fakePerformance.now(); this.duration = 0; },
    PerformanceMeasure: function (name) { this.name = name; this.entryType = 'measure'; this.startTime = 0; this.duration = 0; },
    PerformanceLongTaskTiming: function () { this.entryType = 'longtask'; this.startTime = 0; this.duration = 0; },

    // Timer functions — deferred queue, no depth limits
    setTimeout: function(fn, ms) {
        var id = ++__timerId;
        if (typeof fn === 'function') {
            __timerQueue.push({ fn: fn, args: Array.prototype.slice.call(arguments, 2), id: id, ms: ms || 0 });
        } else if (typeof fn === 'string') {
            __timerQueue.push({ fn: function() { (0, eval)(fn); }, args: [], id: id, ms: ms || 0 });
        }
        return id;
    },
    clearTimeout: function(id) {
        var idx = __timerQueue.findIndex(function(t) { return t.id === id; });
        if (idx >= 0) __timerQueue.splice(idx, 1);
    },
    setInterval: function(fn, ms) {
        return browserAPIs.setTimeout(fn, ms);
    },
    clearInterval: function(id) {
        browserAPIs.clearTimeout(id);
    },
    requestAnimationFrame: function (fn) { var id = ++__timerId; if (typeof fn === 'function') { try { fn(fakePerformance.now()); } catch (e) {} } return id; },
    cancelAnimationFrame: function () {},
    requestIdleCallback: function (fn) { var id = ++__timerId; if (typeof fn === 'function') { try { fn({ didTimeout: false, timeRemaining: function () { return 50; } }); } catch (e) {} } return id; },
    cancelIdleCallback: function () {},
    queueMicrotask: function (fn) { if (typeof fn === 'function') Promise.resolve().then(fn); },

    // Window dimension/display properties
    devicePixelRatio: 2,
    innerWidth: 1920, innerHeight: 1080,
    outerWidth: 1920, outerHeight: 1080,
    scrollX: 0, scrollY: 0,
    pageXOffset: 0, pageYOffset: 0,
    screenX: 0, screenY: 0, screenLeft: 0, screenTop: 0,

    // Window methods
    getComputedStyle: getComputedStyle,
    matchMedia: function (q) { return { matches: false, media: q || '', addListener: function () {}, removeListener: function () {}, addEventListener: function () {}, removeEventListener: function () {}, onchange: null }; },
    addEventListener: function (type, fn, opts) { (_windowListeners[type] = _windowListeners[type] || []).push(fn); },
    removeEventListener: function (type, fn) { _windowListeners[type] = (_windowListeners[type] || []).filter(function (f) { return f !== fn; }); },
    dispatchEvent: function (event) { (_windowListeners[event.type] || []).forEach(function (fn) { try { fn(event); } catch (e) {} }); return true; },
    postMessage: function () {},
    open: function () { return null; },
    close: function () {},
    alert: function () {},
    confirm: function () { return false; },
    prompt: function () { return null; },
    print: function () {},
    focus: function () {},
    blur: function () {},
    scroll: function () {},
    scrollTo: function () {},
    scrollBy: function () {},
    getSelection: function () { return { rangeCount: 0, toString: function () { return ''; } }; },
    resizeTo: function () {},
    resizeBy: function () {},
    moveTo: function () {},
    moveBy: function () {},
    stop: function () {},
    structuredClone: function (obj) { return JSON.parse(JSON.stringify(obj)); },

    // Window state
    name: '',
    closed: false,
    opener: null,
    parent: null,
    top: null,
    frameElement: null,
    frames: [],
    length: 0,
    isSecureContext: true,
    origin: 'https://www.tiktok.com',
    crossOriginIsolated: false,

    // CSS
    CSS: { supports: function () { return true; }, escape: function (s) { return s; } },
    CSSStyleSheet: function () {},
    StyleSheet: function () {},

    // IndexedDB stub
    indexedDB: {
        open: function () { return { addEventListener: function () {}, result: null }; },
        deleteDatabase: function () { return { addEventListener: function () {} }; }
    },

    // Slardar globals
    __SLARDAR_REGISTRY__: safeProxy({}, '__SLARDAR_REGISTRY__'),
    __SLARDAR_DEVTOOLS_GLOBAL_HOOK__: undefined,
};

// ============================================================
// Set of browser API keys that should be wrapped with safeProxy
// (objects where SDK may traverse unknown property chains)
// ============================================================
var safeProxyWrappedAPIs = new Set([
    'navigator', 'document', 'screen', 'location', 'history',
    'performance', 'crypto', 'indexedDB',
    '__SLARDAR_REGISTRY__',
]);

// ============================================================
// Window Proxy — the heart of the hybrid approach
// ============================================================
var windowProxy = new Proxy(function windowTarget(){}, {
    get: function (target, prop) {
        // Self-references
        if (prop === 'window' || prop === 'self' || prop === 'globalThis') return windowProxy;

        // Native builtins — return directly (NOT wrapped)
        if (prop in nativeBuiltins) return nativeBuiltins[prop];

        // SDK-set properties — return as-is (no wrapping)
        if (prop in target) return target[prop];

        // Browser APIs — wrap traversable objects with safeProxy
        if (prop in browserAPIs) {
            var val = browserAPIs[prop];
            if (safeProxyWrappedAPIs.has(prop)) return safeProxy(val, String(prop));
            return val;
        }

        // Symbol properties
        if (typeof prop === 'symbol') return undefined;

        // Unknown property — return a safe callable proxy
        return safeProxy(function(){}, 'window.' + String(prop));
    },
    set: function (target, prop, value) {
        // SDK-set properties (byted_acrawler, _mssdk, etc.) stored directly
        if (prop in browserAPIs) {
            browserAPIs[prop] = value;
        } else {
            target[prop] = value;
        }
        return true;
    },
    has: function (target, prop) {
        if (prop === 'window' || prop === 'self' || prop === 'globalThis') return true;
        if (prop in nativeBuiltins) return true;
        if (prop in browserAPIs) return true;
        if (prop in target) return true;
        return false;
    },
    getOwnPropertyDescriptor: function (target, prop) {
        // Only report own properties that actually exist on target
        var desc = Object.getOwnPropertyDescriptor(target, prop);
        if (desc) return desc;
        return undefined;
    },
    ownKeys: function (target) {
        // Only return keys actually on target to satisfy the invariant
        return Reflect.ownKeys(target);
    }
});

// Wire self-references
browserAPIs.parent = windowProxy;
browserAPIs.top = windowProxy;
fakeDocument.defaultView = windowProxy;

// ============================================================
// Install on globalThis
// ============================================================
globalThis.window = windowProxy;
globalThis.self = windowProxy;
globalThis.document = safeProxy(fakeDocument, 'document');
globalThis.navigator = safeProxy(fakeNavigator, 'navigator');
globalThis.console = browserAPIs.console;
globalThis.screen = safeProxy(fakeScreen, 'screen');
globalThis.location = fakeLocation;
globalThis.history = browserAPIs.history;
globalThis.localStorage = _localStorage;
globalThis.sessionStorage = _sessionStorage;
globalThis.performance = fakePerformance;
globalThis.crypto = fakeCrypto;
// Install fetch with replacement detection
let _realFetch = fakeFetch;
Object.defineProperty(globalThis, 'fetch', {
    configurable: true,
    get() { return _realFetch; },
    set(fn) {
        if (typeof globalThis.__stderr !== 'undefined') {
            globalThis.__stderr.puts('[FETCH-REPLACED] fetch was replaced by SDK!\n');
        }
        _realFetch = fn;
    }
});

// Install XMLHttpRequest with open() replacement detection
globalThis.XMLHttpRequest = FakeXMLHttpRequest;
const _origXHROpen = FakeXMLHttpRequest.prototype.open;
Object.defineProperty(FakeXMLHttpRequest.prototype, 'open', {
    configurable: true,
    get() { return this._openFn || _origXHROpen; },
    set(fn) {
        if (typeof globalThis.__stderr !== 'undefined') {
            globalThis.__stderr.puts('[XHR-OPEN-REPLACED] XHR.open was replaced by SDK!\n');
        }
        this._openFn = fn;
    }
});
globalThis.TextEncoder = FakeTextEncoder;
globalThis.TextDecoder = FakeTextDecoder;
globalThis.Blob = FakeBlob;
globalThis.File = FakeBlob;
globalThis.URL = FakeURL;
globalThis.URLSearchParams = browserAPIs.URLSearchParams;
globalThis.Headers = browserAPIs.Headers;
globalThis.Request = browserAPIs.Request;
globalThis.Response = browserAPIs.Response;
globalThis.AbortController = browserAPIs.AbortController;
globalThis.AbortSignal = browserAPIs.AbortSignal;
globalThis.Event = FakeEvent;
globalThis.CustomEvent = browserAPIs.CustomEvent;
globalThis.MessageEvent = browserAPIs.MessageEvent;
globalThis.MutationObserver = FakeMutationObserver;
globalThis.IntersectionObserver = FakeIntersectionObserver;
globalThis.ResizeObserver = FakeResizeObserver;
globalThis.PerformanceObserver = FakePerformanceObserver;
globalThis.Image = FakeImage;
globalThis.HTMLCanvasElement = FakeCanvasElement;
globalThis.HTMLElement = FakeElement;
globalThis.HTMLDivElement = FakeElement;
globalThis.HTMLImageElement = FakeElement;
globalThis.HTMLScriptElement = FakeElement;
globalThis.Element = FakeElement;
globalThis.Node = FakeElement;
globalThis.NodeList = Array;
globalThis.DOMParser = browserAPIs.DOMParser;
globalThis.WebSocket = browserAPIs.WebSocket;
globalThis.Worker = browserAPIs.Worker;
globalThis.MessageChannel = browserAPIs.MessageChannel;
globalThis.MessagePort = browserAPIs.MessagePort;
globalThis.BroadcastChannel = browserAPIs.BroadcastChannel;
globalThis.WebGLRenderingContext = browserAPIs.WebGLRenderingContext;
globalThis.Storage = FakeStorage;
globalThis.CSSStyleDeclaration = browserAPIs.CSSStyleDeclaration;
globalThis.atob = atob;
globalThis.btoa = btoa;
globalThis.setTimeout = browserAPIs.setTimeout;
globalThis.clearTimeout = browserAPIs.clearTimeout;
globalThis.setInterval = browserAPIs.setInterval;
globalThis.clearInterval = browserAPIs.clearInterval;
globalThis.requestAnimationFrame = browserAPIs.requestAnimationFrame;
globalThis.cancelAnimationFrame = browserAPIs.cancelAnimationFrame;
globalThis.requestIdleCallback = browserAPIs.requestIdleCallback;
globalThis.cancelIdleCallback = browserAPIs.cancelIdleCallback;
globalThis.queueMicrotask = browserAPIs.queueMicrotask;
globalThis.getComputedStyle = getComputedStyle;
globalThis.matchMedia = browserAPIs.matchMedia;
globalThis.postMessage = browserAPIs.postMessage;
globalThis.addEventListener = browserAPIs.addEventListener;
globalThis.removeEventListener = browserAPIs.removeEventListener;
globalThis.dispatchEvent = browserAPIs.dispatchEvent;
globalThis.structuredClone = browserAPIs.structuredClone;
globalThis.indexedDB = browserAPIs.indexedDB;
globalThis.CSS = browserAPIs.CSS;
globalThis.PerformanceMark = browserAPIs.PerformanceMark;
globalThis.PerformanceMeasure = browserAPIs.PerformanceMeasure;
globalThis.PerformanceLongTaskTiming = browserAPIs.PerformanceLongTaskTiming;

// No-op export for compatibility
globalThis.__shimFlushLog = function () {};

// Drain function — call after init to fire all pending timers
globalThis.__drainDeferredTimeouts = function() {
    if (__drainingTimers) return;
    __drainingTimers = true;
    var safety = 0;
    while (__timerQueue.length > 0 && safety < 5000) {
        var timer = __timerQueue.shift();
        try { timer.fn.apply(null, timer.args); } catch(e) {}
        safety++;
    }
    __drainingTimers = false;
};

// Fire DOMContentLoaded and load events
try {
    fakeDocument.dispatchEvent(new FakeEvent('DOMContentLoaded'));
} catch (e) {}
try {
    var loadEvt = new FakeEvent('load');
    (_windowListeners['load'] || []).forEach(function (fn) { try { fn(loadEvt); } catch (e) {} });
} catch (e) {}
