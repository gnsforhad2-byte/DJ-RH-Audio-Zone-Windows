const { app, BrowserWindow, dialog, ipcMain } = require('electron');
app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');
const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

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
let asioSession = null;
let asioSessionStatus = { available: false, running: false };

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


const asioHelperPath = path.join(__dirname, 'build', 'Release', 'asio-helper.exe');

function runAsioList() {
  return new Promise(resolve => {
    let child;
    try {
      child = spawn(asioHelperPath, ['--list'], {
        windowsHide: true,
        stdio: ['ignore', 'pipe', 'pipe']
      });
    } catch (error) {
      return resolve({ devices: [], error: error.message });
    }

    let output = '';
    let finished = false;
    const finish = result => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      resolve(result);
    };

    const timer = setTimeout(() => {
      try { child.kill(); } catch (_) {}
      finish({ devices: [], error: 'ASIO driver discovery timeout' });
    }, 12000);

    child.stdout.setEncoding('utf8');
    child.stdout.on('data', chunk => { output += chunk; });
    child.stderr.on('data', chunk => console.error('[ASIO discovery]', chunk.toString()));
    child.once('error', error => finish({ devices: [], error: error.message }));
    child.once('exit', code => {
      try {
        const result = JSON.parse(output.trim());
        finish({ devices: Array.isArray(result.devices) ? result.devices : [],
                 error: result.error || (code ? 'ASIO discovery failed' : null) });
      } catch (_) {
        finish({ devices: [], error: output.trim() || 'ASIO helper response invalid' });
      }
    });
  });
}

function closeAsioSession() {
  const child = asioSession;
  asioSession = null;
  asioSessionStatus = { available: true, running: false };
  if (!child) return Promise.resolve(true);

  return new Promise(resolve => {
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      resolve(true);
    };
    const timer = setTimeout(() => {
      try { child.kill(); } catch (_) {}
      finish();
    }, 2500);

    child.once('exit', finish);
    child.once('error', finish);
    try {
      if (child.stdin && !child.stdin.destroyed) child.stdin.end();
      else child.kill();
    } catch (_) {
      try { child.kill(); } catch (_) {}
      finish();
    }
  });
}

function openAsioSession(options) {
  return new Promise((resolve, reject) => {
    let child;
    try {
      child = spawn(asioHelperPath, [
        '--play',
        String(options.deviceId),
        String(options.sampleRate),
        String(options.channels)
      ], {
        windowsHide: true,
        stdio: ['pipe', 'pipe', 'pipe']
      });
    } catch (error) {
      return reject(error);
    }

    asioSession = child;
    asioSessionStatus = { available: true, running: false };

    let output = '';
    let settled = false;
    const fail = error => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      if (asioSession === child) asioSession = null;
      try { child.kill(); } catch (_) {}
      reject(error instanceof Error ? error : new Error(String(error)));
    };

    const timer = setTimeout(
      () => fail(new Error('ASIO driver চালু হতে ২০ সেকেন্ডের বেশি সময় নিয়েছে।')),
      20000
    );

    child.stdout.setEncoding('utf8');
    const onData = chunk => {
      output += chunk;
      const newline = output.indexOf('\n');
      if (newline < 0) return;

      child.stdout.removeListener('data', onData);
      let result;
      try { result = JSON.parse(output.slice(0, newline).trim()); }
      catch (_) { return fail(new Error('ASIO helper-এর response সঠিক নয়।')); }

      if (!result.ready || !result.running) {
        return fail(new Error(result.error || 'ASIO stream চালু হয়নি।'));
      }

      settled = true;
      clearTimeout(timer);
      asioSessionStatus = {
        available: true,
        running: true,
        actualSampleRate: result.actualSampleRate,
        channels: result.channels
      };
      resolve(asioSessionStatus);
    };

    child.stdout.on('data', onData);
    child.stderr.on('data', chunk => console.error('[ASIO helper]', chunk.toString()));
    child.once('error', fail);
    child.once('exit', code => {
      if (!settled) fail(new Error(`ASIO helper বন্ধ হয়েছে (${code})`));
      if (asioSession === child) {
        asioSession = null;
        asioSessionStatus = { available: true, running: false };
      }
    });
  });
}

ipcMain.handle('asio:get-devices', () => runAsioList());

ipcMain.handle('asio:open', async (_event, options = {}) => {
  if (asioSession) await closeAsioSession();

  const deviceId = Number(options.deviceId);
  const sampleRate = Number(options.sampleRate);
  const channels = Number(options.channels);

  if (!Number.isInteger(deviceId) || !Number.isInteger(sampleRate) ||
      !Number.isInteger(channels) || channels < 1 || channels > 2) {
    throw new Error('ASIO device settings সঠিক নয়।');
  }
  if (process.platform !== 'win32') {
    throw new Error('ASIO শুধু Windows-এ চালানো যাবে।');
  }
  if (!fs.existsSync(asioHelperPath)) {
    throw new Error('ASIO helper পাওয়া যায়নি। Windows native build প্রয়োজন।');
  }
  return openAsioSession({ deviceId, sampleRate, channels });
});

ipcMain.handle('asio:close', () => closeAsioSession());

ipcMain.handle('asio:status', () => ({
  ...asioSessionStatus,
  available: process.platform === 'win32' && fs.existsSync(asioHelperPath)
}));

ipcMain.on('asio:write', (_event, samples) => {
  const child = asioSession;
  if (!child || !asioSessionStatus.running) return;
  if (!(samples instanceof Float32Array)) return;
  if (!child.stdin || child.stdin.destroyed) return;

  // Bound queued PCM so a slow helper cannot grow memory indefinitely.
  if (child.stdin.writableLength > 1024 * 1024) return;

  try {
    const bytes = Buffer.from(samples.buffer, samples.byteOffset, samples.byteLength);
    child.stdin.write(bytes);
  } catch (error) {
    console.error('ASIO audio write failed:', error);
  }
});


app.on('before-quit', () => {
  closeAsioSession();
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
