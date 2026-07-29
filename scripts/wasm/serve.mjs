// Minimal static file server for the wasm app build (no dependencies; runs on
// the emsdk-bundled node via serve-app.ps1). Serves correct MIME types for
// .wasm/.js/.data, which generic file servers sometimes get wrong.
// Usage: node serve.mjs <root-dir> [port] [--open]
import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize, sep } from 'node:path';

// Flags are filtered out before the positional arguments so --open can sit
// anywhere on the command line.
const args = process.argv.slice(2);
const openInDefaultBrowser = args.includes('--open');
const positional = args.filter((argument) => !argument.startsWith('--'));
const root = normalize(positional[0] ?? '.');
const port = Number(positional[1] ?? 8973);
const types = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
  '.data': 'application/octet-stream',
  '.json': 'application/json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

createServer(async (request, response) => {
  try {
    const url = new URL(request.url, 'http://localhost');
    let pathname = decodeURIComponent(url.pathname);
    if (pathname.endsWith('/')) {
      pathname += 'patchy.html';
    }
    const file = normalize(join(root, pathname));
    if (file !== root && !file.startsWith(root + sep)) {
      throw new Error('outside root');
    }
    const body = await readFile(file);
    response.writeHead(200, {
      'Content-Type': types[extname(file).toLowerCase()] ?? 'application/octet-stream',
      'Cache-Control': 'no-store',
    });
    response.end(body);
  } catch {
    response.writeHead(404);
    response.end('not found');
  }
}).listen(port, '127.0.0.1', () => {
  const url = `http://localhost:${port}/`;
  console.log(`serving ${root} at ${url}patchy.html`);
  // Opening from the listen callback is the only race-free moment: the socket
  // is accepting before the browser is told to load anything.
  if (openInDefaultBrowser) {
    open_url(url);
  }
});

// Hands the URL to the OS default browser. Detached and stdio-ignored so
// Ctrl+C on the server never reaches the browser and a chatty opener cannot
// block; the error handler matters because a missing opener (xdg-open on a
// bare container) arrives as an async 'error' event that would otherwise take
// the server down with it.
function open_url(url) {
  const [command, commandArgs] =
    process.platform === 'win32'
      ? ['cmd', ['/c', 'start', '', url]]
      : process.platform === 'darwin'
        ? ['open', [url]]
        : ['xdg-open', [url]];
  const child = spawn(command, commandArgs, { detached: true, stdio: 'ignore' });
  child.on('error', () => {
    console.log(`could not open a browser automatically; open ${url} yourself`);
  });
  child.unref();
}
