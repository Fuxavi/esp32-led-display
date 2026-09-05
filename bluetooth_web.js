
// ==================== BLE UUIDs (must match ESP32 code) ====================
const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const TX_CHAR_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';  // Notify (ESP32 -> Web)
const RX_CHAR_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';  // Write   (Web -> ESP32)

const fileInput =
    document.getElementById('fileInput');

const uploadBtn =
    document.getElementById('uploadBtn');

const progressText =
    document.getElementById('progressText');

// ==================== State ====================
let bluetoothDevice = null;
let rxCharacteristic = null;
let txCharacteristic = null;
let receiveBuffer = '';
let filesystemFiles = [];
let readingFilesystem = false;

// ==================== DOM elements ====================
const connectBtn = document.getElementById('connectBtn');
const disconnectBtn = document.getElementById('disconnectBtn');
const ledOnBtn = document.getElementById('ledOnBtn');
const ledOffBtn = document.getElementById('ledOffBtn');
const sendBtn = document.getElementById('sendBtn');
const dirSelect = document.getElementById('dirSelect');
const clearBtn = document.getElementById('clearBtn');
const input = document.getElementById('input');
const messages = document.getElementById('messages');
const statusDot = document.getElementById('statusDot');
const statusText = document.getElementById('statusText');
const refreshFilesBtn = document.getElementById('refreshFilesBtn');
const filesystemStatus = document.getElementById('filesystemStatus');
const filesystemBody = document.getElementById('filesystemBody');

// ==================== Check browser support ====================
if (!navigator.bluetooth) {
    displayMessage('Web Bluetooth API is not supported in this browser. Please use Chrome or Edge.', 'system');
    connectBtn.disabled = true;
}

uploadBtn.addEventListener(
    'click',
    uploadFile
);

refreshFilesBtn.addEventListener(
    'click',
    refreshFilesystem
);

// ==================== Connect ====================
connectBtn.addEventListener('click', async () => {
    try {
    displayMessage('Searching for Bluetooth devices...', 'system');
    bluetoothDevice = await navigator.bluetooth.requestDevice({
        filters: [{ services: [SERVICE_UUID] }]
    });

    bluetoothDevice.addEventListener('gattserverdisconnected', onDisconnected);

    displayMessage('Connecting to ' + bluetoothDevice.name + '...', 'system');
    const server = await bluetoothDevice.gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);

    txCharacteristic = await service.getCharacteristic(TX_CHAR_UUID);
    rxCharacteristic = await service.getCharacteristic(RX_CHAR_UUID);

    // Subscribe to notifications (ESP32 -> Web)
    await txCharacteristic.startNotifications();
    txCharacteristic.addEventListener('characteristicvaluechanged', onValueChanged);

    updateUIConnected();
    displayMessage('Connected to ' + bluetoothDevice.name, 'system');
    } catch (error) {
    displayMessage('Connection failed: ' + error.message, 'system');
    }
});

// ==================== Disconnect ====================
disconnectBtn.addEventListener('click', () => {
    if (bluetoothDevice && bluetoothDevice.gatt.connected) {
    bluetoothDevice.gatt.disconnect();
    }
});

function uint32ToBytes(value) {

    return new Uint8Array([
    value & 0xFF,
    (value >> 8) & 0xFF,
    (value >> 16) & 0xFF,
    (value >> 24) & 0xFF
    ]);
}

function onDisconnected() {
    rxCharacteristic = null;
    txCharacteristic = null;
    bluetoothDevice = null;
    receiveBuffer = '';
    updateUIDisconnected();
    displayMessage('Disconnected', 'system');
}

async function uploadFile() {

    if (!rxCharacteristic) {

    displayMessage(
        'Not connected',
        'system'
    );

    return;
    }

    const file = fileInput.files[0];
    if (!file) {

    displayMessage(
        'Please select a file first',
        'system'
    );
    return;
    }

    const name = dirSelect.value + file.name
    displayMessage(
    'Starting upload: ' +
    name +
    ' (' +
    file.size +
    ' bytes)',
    'system'
    );

    // FILE BEGIN

    const fileNameBytes =
    new TextEncoder().encode(name);

    const sizeBytes =
    uint32ToBytes(file.size);

    const beginPacket =
    new Uint8Array(
        1 +
        4 +
        fileNameBytes.length
    );

    beginPacket[0] = 0x01;

    beginPacket.set(
    sizeBytes,
    1
    );

    beginPacket.set(
    fileNameBytes,
    5
    );

    await rxCharacteristic.writeValue(
    beginPacket
    );

    // READ FILE

    const buffer =
    await file.arrayBuffer();

    const data =
    new Uint8Array(buffer);


    // Tamaño del fragmento
    const CHUNK_SIZE = 500;

    let chunkNumber = 0;


    // =====================================
    // SEND CHUNKS
    // =====================================

    for (
    let offset = 0;
    offset < data.length;
    offset += CHUNK_SIZE
    ) {

    const chunk =
        data.slice(
        offset,
        offset + CHUNK_SIZE
        );


    const packet =
        new Uint8Array(
        5 +
        chunk.length
        );


    // Tipo FILE_CHUNK

    packet[0] = 0x02;

    // Número de chunk

    packet[1] =
        chunkNumber & 0xFF;

    packet[2] =
        (chunkNumber >> 8) & 0xFF;

    packet[3] =
        (chunkNumber >> 16) & 0xFF;

    packet[4] =
        (chunkNumber >> 24) & 0xFF;

    // Datos
    packet.set(
        chunk,
        5
    );

    // Enviar

    await rxCharacteristic.writeValue(
        packet
    );

    // Actualizar progreso

    const progress =
        Math.round(
        (
            Math.min(
            offset + CHUNK_SIZE,
            data.length
            )
            /
            data.length
        )
        * 100
        );

    progressText.textContent =
        'Uploading: ' +
        progress +
        '%';


    chunkNumber++;
    }

    // =====================================
    // FILE END
    // =====================================

    await rxCharacteristic.writeValue(
    new Uint8Array([0x03])
    );

    progressText.textContent =
    'Upload sent. Waiting for ESP32 confirmation...';

    displayMessage(
    'File upload finished',
    'system'
    );
}

// ==================== Send message ====================
async function sendMessage(msg) {
    if (!rxCharacteristic) {
    displayMessage('Not connected. Cannot send.', 'system');
    return;
    }
    try {
    const data = new TextEncoder().encode(msg + '\n');
    await rxCharacteristic.writeValue(data);
    displayMessage(msg, 'sent');
    } catch (error) {
    displayMessage('Send failed: ' + error.message, 'system');
    }
}

sendBtn.addEventListener('click', () => {
    const msg = input.value.trim();
    if (msg) {
    sendMessage(msg);
    input.value = '';
    }
});

input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
    sendBtn.click();
    }
});

// ==================== LED control buttons ====================
ledOnBtn.addEventListener('click', () => sendMessage('/ledon'));
ledOffBtn.addEventListener('click', () => sendMessage('/ledoff'));

// ==================== Clear messages ====================
clearBtn.addEventListener('click', () => {
    messages.innerHTML = '';
});

function renderFilesystem() {
    filesystemBody.innerHTML = '';

    if (filesystemFiles.length === 0) {
    const row = document.createElement('tr');
    row.innerHTML = `
        <td
        colspan="3"
        class="empty-filesystem"
        >
        No files
        </td>
    `;
    filesystemBody.appendChild(row);
    return;
    }
    dirSelect.innerHTML = '';

    filesystemFiles
        .filter(file => file.isDirectory)
        .forEach(dir => {
        const option = document.createElement('option');
        option.value = dir.name;
        option.textContent = dir.name;
        dirSelect.appendChild(option);
        });

    filesystemFiles.forEach(file => {
    const row = document.createElement('tr');

    // Nombre
    const nameCell = document.createElement('td');
    nameCell.textContent = file.name;

    // Tamaño

    const sizeCell =
        document.createElement('td');

    sizeCell.textContent =
        formatFileSize(file.size);


    // =========================
    // Acciones
    // =========================

    const actionCell =
        document.createElement('td');

    const deleteButton =
        document.createElement('button');

    deleteButton.textContent = file.isDirectory ? '📁' : '🗑️';

    deleteButton.className =
        'btn-delete-file';

    deleteButton.title =
        'Delete ' + file.name;

    deleteButton.addEventListener(
        'click',
        () => deleteFromESP32(file.name, file.isDirectory)
    );

    actionCell.appendChild(
        deleteButton
    );

    row.appendChild(nameCell);
    row.appendChild(sizeCell);
    row.appendChild(actionCell);

    filesystemBody.appendChild(row);

    });
}

function formatFileSize(bytes) {
    if (bytes < 1024) {

    return bytes + ' B';

    }
    if (bytes < 1024 * 1024) {
    return (
        (bytes / 1024).toFixed(1) +
        ' KB'
    );
    }
    return (
    (bytes / (1024 * 1024)).toFixed(2) +
    ' MB'
    );
}
// ==================== Receive data (ESP32 -> Web) ====================
function onValueChanged(event) {

    const value = event.target.value;

    let text = '';

    for (
    let i = 0;
    i < value.byteLength;
    i++
    ) {

    text += String.fromCharCode(
        value.getUint8(i)
    );
    }

    receiveBuffer += text;

    let idx;

    while (
    (idx = receiveBuffer.indexOf('\n')) >= 0
    ) {

    const msg =
        receiveBuffer
        .substring(0, idx)
        .trim();

    receiveBuffer =
        receiveBuffer.substring(idx + 1);

    if (msg.length === 0) {
        continue;
    }

    console.log(msg);

    // =====================================
    // FILESYSTEM
    // =====================================

    if (msg === '[FS] BEGIN') {

        filesystemFiles = [];

        filesystemBody.innerHTML = '';

        readingFilesystem = true;

        filesystemStatus.textContent =
        'Reading files...';
        continue;
    }

    if (msg.startsWith('[FS] FILE|')) {

        const parts =
        msg.split('|');

        if (parts.length >= 3) {

        const fileName =
            parts[1];

        const fileSize =
            parseInt(parts[2]);

        filesystemFiles.push({
            name: fileName,
            isDirectory: false,
            size: fileSize
        });

        renderFilesystem();
        }

        continue;
    }

    if (msg.startsWith('[FS] DIR|')) {

        const parts =
        msg.split('|');

        if (parts.length >= 2) {

        const fileName =
            parts[1];

        filesystemFiles.push({
            name: fileName,
            isDirectory: true,
            size: 0
        });

        renderFilesystem();
        }

        continue;
    }

    if (msg.startsWith('[FS] DELETED|')) {

        const fileName =
        msg.substring(
            '[FS] DELETED|'.length
        );


        displayMessage(
        'File deleted: ' + fileName,
        'system'
        );


        filesystemStatus.textContent =
        'Deleted: ' + fileName;


        // Eliminarlo inmediatamente de la lista
        filesystemFiles =
        filesystemFiles.filter(
            file => file.name !== fileName
        );


        renderFilesystem();


        continue;
    }

    if (msg.startsWith('[FS] DELETE_ERROR|')) {

        const parts =
        msg.split('|');

        const errorMessage =
        parts.slice(1).join(' | ');


        displayMessage(
        'Delete error: ' + errorMessage,
        'system'
        );
        filesystemStatus.textContent =
        'Delete error';


        continue;
    }
    if (msg === '[FS] END') {

        readingFilesystem = false;

        renderFilesystem();

        filesystemStatus.textContent =
        filesystemFiles.length +
        ' file(s) found';

        continue;
    }
    if (msg === '[FS] ERROR') {

        readingFilesystem = false;

        filesystemStatus.textContent =
        'Error reading filesystem';

        continue;
    }


    // =====================================
    // NORMAL MESSAGE
    // =====================================

    displayMessage(
        msg,
        'received'
    );
    }
}

// ==================== UI helpers ====================
function displayMessage(msg, type) {
    const time = new Date().toLocaleTimeString();
    const div = document.createElement('div');
    div.className = 'message ' + type;
    div.textContent = '[' + time + '] ' + msg;
    messages.appendChild(div);
    messages.scrollTop = messages.scrollHeight;
}

function updateUIConnected() {
    fileInput.disabled = false;
    uploadBtn.disabled = false;
    dirSelect.disabled = false;
    refreshFilesBtn.disabled = false;
    connectBtn.disabled = true;
    disconnectBtn.disabled = false;
    ledOnBtn.disabled = false;
    ledOffBtn.disabled = false;
    sendBtn.disabled = false;
    input.disabled = false;
    statusDot.className = 'status-dot connected';
    statusText.textContent = 'Connected';
    input.focus();
}

function updateUIDisconnected() {
    fileInput.disabled = true;
    uploadBtn.disabled = true;
    dirSelect.disabled = true;
    refreshFilesBtn.disabled = true;
    connectBtn.disabled = false;
    disconnectBtn.disabled = true;
    ledOnBtn.disabled = true;
    ledOffBtn.disabled = true;
    sendBtn.disabled = true;
    input.disabled = true;
    statusDot.className = 'status-dot disconnected';
    statusText.textContent = 'Disconnected';
}

async function refreshFilesystem() {

    if (!rxCharacteristic) {

    filesystemStatus.textContent =
        'Not connected';

    return;
    }

    filesystemFiles = [];

    filesystemBody.innerHTML = '';

    filesystemStatus.textContent =
    'Reading filesystem...';

    readingFilesystem = true;

    try {

    await sendMessage('/files');

    } catch (error) {

    readingFilesystem = false;

    filesystemStatus.textContent =
        'Error: ' + error.message;
    }
}

async function deleteFromESP32(fileName, isDirectory) {

    if (!rxCharacteristic) {

    displayMessage(
        'Not connected',
        'system'
    );

    return;
    }

    const confirmed = confirm(
    '¿Seguro que quieres borrar este ' + (isDirectory ? 'directorio' : 'archivo') + '?:\n\n' + fileName
    );

    if (!confirmed) return;

    try {
    filesystemStatus.textContent = 'Deleting ' + fileName + '...';
    const command = (isDirectory ? '/rmdir ':'/delete ') + fileName;
    await sendMessage(command);

    displayMessage(
        'Delete requested: ' + fileName,
        'system'
    );

    } catch (error) {

    filesystemStatus.textContent =
        'Delete error';

    displayMessage(
        'Delete failed: ' + error.message,
        'system'
    );

    }
}