const http = require("http");
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const port = Number(process.argv[2] || 8768);
const host = "127.0.0.1";

const types = new Map([
  [".html", "text/html; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".out", "text/plain; charset=utf-8"],
  [".txt", "text/plain; charset=utf-8"],
]);

const server = http.createServer((request, response) => {
  const url = new URL(request.url, `http://${host}:${port}`);
  let filePath = path.join(root, decodeURIComponent(url.pathname));

  if (url.pathname === "/" || url.pathname.endsWith("/")) {
    filePath = path.join(filePath, "index.html");
  }

  const resolved = path.resolve(filePath);
  if (!resolved.startsWith(root)) {
    response.writeHead(403);
    response.end("Forbidden");
    return;
  }

  fs.readFile(resolved, (error, data) => {
    if (error) {
      response.writeHead(404);
      response.end("Not found");
      return;
    }

    response.writeHead(200, {
      "content-type": types.get(path.extname(resolved)) || "application/octet-stream",
      "cache-control": "no-store",
    });
    response.end(data);
  });
});

server.listen(port, host, () => {
  console.log(`Memory Cache Visualizer: http://${host}:${port}/visualizer/`);
});
