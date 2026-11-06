const { app, BrowserWindow, dialog, ipcMain } = require('electron');
const http = require('http');
const fs = require('fs');
const path = require('path');

const HOST = 'localhost';
const PORT = 43127;
const ROOT = app.getAppPath();

const mimeTypes = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.mp3': 'audio/mpeg',
  '.wav': 'audio/wav',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon'
};

let mainWindow;
let localServer;
let wasapiAddon = null;

try {
  wasapiAddon = require(path.join(__dirname, 'build', 'Release', 'wasapi.node'));
} catch (error) {
  console.warn('WASAPI native module unavailable:', error.message);
}

ipcMain.handle('wasapi:get-devices', () => {
  if (!wasapiAddon) return [];
  return wasapiAddon.getDevices();
});

ipcMain.handle('wasapi:open', (_event, options = {}) => {
  if (!wasapiAddon) throw new Error('WASAPI native module পাওয়া যায়নি।');
  const deviceId = Number(options.deviceId);
  const sampleRate = Number(options.sampleRate);
  const channels = Number(options.channels);
  if (!Number.isInteger(deviceId) || !Number.isInteger(sampleRate) ||
      !Number.isInteger(channels)) {
    throw new Error('অডিও ডিভাইসের সেটিংস সঠিক নয়।');
  }
  wasapiAddon.open({ deviceId, sampleRate, channels });
  return wasapiAddon.getStatus();
});

ipcMain.handle('wasapi:close', () => {
  if (wasapiAddon) wasapiAddon.close();
  return true;
});

ipcMain.handle('wasapi:status', () => {
  return wasapiAddon ? wasapiAddon.getStatus() : {
    available: false, opened: false, running: false
  };
});

ipcMain.on('wasapi:write', (_event, samples) => {
  if (!wasapiAddon || !wasapiAddon.getStatus().running) return;
  if (!(samples instanceof Float32Array)) return;
  try {
    wasapiAddon.write(samples);
  } catch (error) {
    console.error('WASAPI audio write failed:', error);
  }
});

app.on('before-quit', () => {
  try {
    if (wasapiAddon) wasapiAddon.close();
  } catch (error) {
    console.error('WASAPI close failed:', error);
  }
});

function startLocalServer() {
  localServer = http.createServer((req, res) => {
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      res.writeHead(405);
      return res.end('Method Not Allowed');
    }

    let pathname;
    try {
      pathname = decodeURIComponent(new URL(req.url, `http://${HOST}`).pathname);
    } catch {
      res.writeHead(400);
      return res.end('Bad Request');
    }

    if (pathname === '/') pathname = '/index.html';

    const filePath = path.resolve(ROOT, `.${pathname}`);
    if (filePath !== ROOT && !filePath.startsWith(ROOT + path.sep)) {
      res.writeHead(403);
      return res.end('Forbidden');
    }

    fs.stat(filePath, (error, stats) => {
      if (error || !stats.isFile()) {
        res.writeHead(404);
        return res.end('Not Found');
      }

      res.writeHead(200, {
        'Content-Type': mimeTypes[path.extname(filePath).toLowerCase()] || 'application/octet-stream',
        'Content-Length': stats.size,
        'X-Content-Type-Options': 'nosniff',
        'Cache-Control': 'no-store'
      });

      if (req.method === 'HEAD') return res.end();

      const stream = fs.createReadStream(filePath);
      stream.on('error', () => {
        if (!res.headersSent) res.writeHead(500);
        res.end();
      });
      stream.pipe(res);
    });
  });

  return new Promise((resolve, reject) => {
    localServer.once('error', reject);
    localServer.listen(PORT, HOST, () => {
      localServer.removeListener('error', reject);
      resolve();
    });
  });
}

async function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1280,
    height: 820,
    minWidth: 900,
    minHeight: 600,
    show: true,
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: true
    }
  });

  const appUrl = `http://${HOST}:${PORT}/`;

  mainWindow.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  mainWindow.webContents.on('will-navigate', (event, url) => {
    if (!url.startsWith(appUrl)) event.preventDefault();
  });

  await mainWindow.loadURL(appUrl);
}

const gotLock = app.requestSingleInstanceLock();

if (!gotLock) {
  app.quit();
} else {
  app.whenReady().then(async () => {
    try {
      await startLocalServer();
      await createWindow();
    } catch (error) {
      dialog.showErrorBox(
        'DJ RH Audio Zone',
        `অ্যাপ চালু করা যায়নি। Local server port ${PORT} ব্যবহার করা যাচ্ছে না।\n\n${error.message}`
      );
      app.quit();
    }
  });

  app.on('second-instance', () => {
    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore();
      mainWindow.focus();
    }
  });

  app.on('window-all-closed', () => {
    if (localServer) localServer.close();
    app.quit();
  });
}
