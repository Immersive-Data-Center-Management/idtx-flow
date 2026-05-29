/**
 * coop_static_server.js — Cross-Origin Isolated static file server for Godot web exports.
 *
 * Godot's multi-threaded web build requires SharedArrayBuffer, which browsers only
 * allow in "cross-origin isolated" contexts. That means every HTTP response must carry:
 *   Cross-Origin-Opener-Policy:  same-origin
 *   Cross-Origin-Embedder-Policy: require-corp
 * Standard servers (Python's http.server, VS Code Live Server, etc.) do not send these
 * headers, so SharedArrayBuffer is unavailable and Godot's thread pool fails to start.
 * This server injects those headers on every response.
 *
 * Usage:
 *   node tools/coop_static_server.js [root_dir] [port]
 *
 *   root_dir  — directory to serve (default: current working directory)
 *   port      — TCP port to listen on (default: 8091)
 *
 * Example (serve a Godot web export from BUILDS3 on port 8091):
 *   node tools/coop_static_server.js C:/Godot/web-test/BUILDS3 8091
 *
 * Then open http://localhost:8091 in a browser that supports WebAssembly threads
 * (Chrome, Edge, Firefox with COOP/COEP headers present).
 */
const http = require('http');
const fs = require('fs');
const path = require('path');

const root = process.argv[2] || process.cwd();
const port = Number(process.argv[3] || 8091);

const mime = {
  '.html': 'text/html; charset=UTF-8',
  '.js': 'text/javascript; charset=UTF-8',
  '.mjs': 'text/javascript; charset=UTF-8',
  '.css': 'text/css; charset=UTF-8',
  '.json': 'application/json; charset=UTF-8',
  '.wasm': 'application/wasm',
  '.pck': 'application/octet-stream',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.txt': 'text/plain; charset=UTF-8'
};

function setCommonHeaders(res) {
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  res.setHeader('Cross-Origin-Resource-Policy', 'cross-origin');
  res.setHeader('Access-Control-Allow-Origin', '*');
}

function safePath(urlPath) {
  const decoded = decodeURIComponent(urlPath.split('?')[0]);
  const normalized = path.normalize(decoded).replace(/^([.][.][/\\])+/, '');
  return normalized === path.sep ? '' : normalized;
}

const server = http.createServer((req, res) => {
  const reqPath = req.url === '/' ? '/index.html' : req.url;
  const relPath = safePath(reqPath);
  const filePath = path.join(root, relPath);

  if (!filePath.startsWith(path.resolve(root))) {
    setCommonHeaders(res);
    res.statusCode = 403;
    res.end('Forbidden');
    return;
  }

  fs.stat(filePath, (err, stat) => {
    if (err || !stat.isFile()) {
      setCommonHeaders(res);
      res.statusCode = 404;
      res.end('Not found');
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    setCommonHeaders(res);
    res.setHeader('Content-Type', mime[ext] || 'application/octet-stream');
    res.setHeader('Content-Length', stat.size);

    const stream = fs.createReadStream(filePath);
    stream.on('error', () => {
      res.statusCode = 500;
      res.end('Internal server error');
    });
    stream.pipe(res);
  });
});

server.listen(port, '127.0.0.1', () => {
  console.log(`Serving ${path.resolve(root)} on http://127.0.0.1:${port}`);
});
