const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('audioDriver', {
  getDevices: () => ipcRenderer.invoke('wasapi:get-devices'),
  open: options => ipcRenderer.invoke('wasapi:open', options),
  close: () => ipcRenderer.invoke('wasapi:close'),
  status: () => ipcRenderer.invoke('wasapi:status'),
  write: samples => ipcRenderer.send('wasapi:write', samples),
  getAsioDevices: () => ipcRenderer.invoke('asio:get-devices'),
  openAsio: options => ipcRenderer.invoke('asio:open', options),
  closeAsio: () => ipcRenderer.invoke('asio:close'),
  statusAsio: () => ipcRenderer.invoke('asio:status'),
  writeAsio: samples => ipcRenderer.send('asio:write', samples)
});
