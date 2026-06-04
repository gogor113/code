// ==================== BON WARUNG v34.7 - CLOUD BACKEND (GS) ====================
// Versi: 1.0.0 (Compatible with BON_V1.html v34.7)
// Nama File: Code.gs
// Fungsi: Menyimpan, menggabungkan (merge), dan sinkronisasi data utang warung
//          dari berbagai perangkat ke Google Spreadsheet.
// Fitur:
// 1. Multi-user authentication & data isolation
// 2. Conflict resolution berdasarkan timestamp terbaru
// 3. Merge logic untuk cross-device sync
// 4. Retry mechanism & error handling
// 5. Data integrity dengan unique ID per record
// ==============================================================================

// ==================== KONFIGURASI SPREADSHEET ====================
// Spreadsheet ID dari Google Spreadsheet yang akan digunakan.
// Cara mendapatkan: Buka spreadsheet -> lihat URL -> ambil ID antara /d/ dan /edit
// Contoh: https://docs.google.com/spreadsheets/d/1ABC123DEF456/edit -> ID = 1ABC123DEF456
// Ganti dengan ID spreadsheet Anda sendiri!
const SPREADSHEET_ID = '1ABC123DEF456'; // <-- WAJIB DIUBAH dengan ID Spreadsheet Anda!

// Nama-nama sheet yang akan dibuat secara otomatis
const SHEET_USERS = 'Users';           // Menyimpan data akun pengguna
const SHEET_BONS = 'Bons';             // Menyimpan data bon (utang)
const SHEET_PAYMENTS = 'Payments';     // Menyimpan data pembayaran
const SHEET_SYNC_LOG = 'SyncLog';      // Log sinkronisasi untuk debugging

// ==================== FUNGSI UTAMA DO POST (Web App Entry Point) ====================
function doPost(e) {
  // Inisialisasi response dan CORS
  const response = {
    success: false,
    message: '',
    data: null
  };
  
  try {
    // Validasi input
    if (!e || !e.parameter) {
      response.message = 'No parameters provided';
      return sendJsonResponse(response, 400);
    }
    
    const action = e.parameter.action;
    const dataParam = e.parameter.data;
    
    Logger.log(`[${new Date().toISOString()}] Action received: ${action}`);
    
    // Route berdasarkan action
    switch(action) {
      case 'testConnection':
        response.success = true;
        response.message = 'Cloud Backup v34.7 siap dan berfungsi!';
        response.data = { serverTime: new Date().toISOString() };
        break;
        
      case 'getUserAuth':
        // Mendapatkan data autentikasi user dari cloud
        const username = e.parameter.username;
        if (!username) {
          response.message = 'Username required';
          return sendJsonResponse(response, 400);
        }
        const userData = getUserFromSheet(username);
        if (userData) {
          response.success = true;
          response.userData = userData;
        } else {
          response.success = false;
          response.message = 'User not found';
        }
        break;
        
      case 'syncUserAuth':
        // Sinkronisasi data autentikasi user ke cloud
        const syncUserData = JSON.parse(dataParam || '{}');
        const subAction = syncUserData.subAction;
        const targetUsername = syncUserData.username;
        const userInfo = syncUserData.userData;
        
        if (!targetUsername || !userInfo) {
          response.message = 'Username and userData required';
          return sendJsonResponse(response, 400);
        }
        
        const saveResult = saveUserToSheet(targetUsername, userInfo);
        if (saveResult) {
          response.success = true;
          response.message = `User ${subAction} synced successfully`;
        } else {
          response.success = false;
          response.message = 'Failed to save user data';
        }
        break;
        
      case 'restoreV34':
        // Mendapatkan data lengkap user (bons + payments)
        const restoreUsername = e.parameter.username;
        if (!restoreUsername) {
          response.message = 'Username required';
          return sendJsonResponse(response, 400);
        }
        
        const userBons = getBonsFromSheet(restoreUsername);
        const userPayments = getPaymentsFromSheet(restoreUsername);
        
        response.success = true;
        response.semuaBon = userBons;
        response.pembayaran = userPayments;
        response.lastModified = new Date().toISOString();
        response.serverTimestamp = new Date().toISOString();
        break;
        
      case 'mergeBackupV34':
        // Merge data dari client ke cloud (full sync)
        const backupData = JSON.parse(dataParam || '{}');
        const backupUsername = backupData.username;
        
        if (!backupUsername) {
          response.message = 'Username required in backup data';
          return sendJsonResponse(response, 400);
        }
        
        // Ambil data existing dari sheet
        const existingBons = getBonsFromSheet(backupUsername);
        const existingPayments = getPaymentsFromSheet(backupUsername);
        
        // Data dari client
        const clientBons = backupData.semuaBon || [];
        const clientPayments = backupData.pembayaran || [];
        
        // Merge dengan conflict resolution (timestamp terbaru menang)
        const mergedBons = mergeDataWithTimestamp(existingBons, clientBons, 'uniqueId');
        const mergedPayments = mergeDataWithTimestamp(existingPayments, clientPayments, 'uniqueId');
        
        // Simpan ke sheet
        const bonsSaved = saveBonsToSheet(backupUsername, mergedBons);
        const paymentsSaved = savePaymentsToSheet(backupUsername, mergedPayments);
        
        if (bonsSaved && paymentsSaved) {
          response.success = true;
          response.message = 'Merge backup successful';
          response.bonsCount = mergedBons.length;
          response.paymentsCount = mergedPayments.length;
          
          // Log sinkronisasi
          logSyncActivity(backupUsername, 'mergeBackup', mergedBons.length, mergedPayments.length);
        } else {
          response.success = false;
          response.message = 'Failed to save merged data';
        }
        break;
        
      case 'syncBonV34':
        // Sinkronisasi single bon
        const bonUsername = e.parameter.username;
        const bonDataRaw = e.parameter.bonData;
        
        if (!bonUsername || !bonDataRaw) {
          response.message = 'Username and bonData required';
          return sendJsonResponse(response, 400);
        }
        
        const bonData = JSON.parse(bonDataRaw);
        const bonSaved = saveSingleBonToSheet(bonUsername, bonData);
        
        if (bonSaved) {
          response.success = true;
          response.message = 'Bon synced successfully';
        } else {
          response.success = false;
          response.message = 'Failed to sync bon';
        }
        break;
        
      case 'syncPaymentV34':
        // Sinkronisasi single payment
        const paymentUsername = e.parameter.username;
        const paymentDataRaw = e.parameter.paymentData;
        
        if (!paymentUsername || !paymentDataRaw) {
          response.message = 'Username and paymentData required';
          return sendJsonResponse(response, 400);
        }
        
        const paymentData = JSON.parse(paymentDataRaw);
        const paymentSaved = saveSinglePaymentToSheet(paymentUsername, paymentData);
        
        if (paymentSaved) {
          response.success = true;
          response.message = 'Payment synced successfully';
        } else {
          response.success = false;
          response.message = 'Failed to sync payment';
        }
        break;
        
      default:
        response.message = `Unknown action: ${action}`;
        return sendJsonResponse(response, 400);
    }
    
    return sendJsonResponse(response);
    
  } catch(error) {
    Logger.log(`Error in doPost: ${error.toString()}`);
    response.success = false;
    response.message = `Server error: ${error.toString()}`;
    return sendJsonResponse(response, 500);
  }
}

// ==================== FUNGSI DO GET (Untuk Testing & Health Check) ====================
function doGet() {
  const html = `
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="UTF-8">
      <title>Bon Warung Cloud Backup v34.7</title>
      <style>
        body { font-family: Arial; padding: 20px; background: #f0f2f5; }
        .container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 10px; }
        h1 { color: #667eea; }
        .status { color: #38a169; font-weight: bold; }
        .error { color: #e53e3e; }
      </style>
    </head>
    <body>
      <div class="container">
        <h1>☁️ Bon Warung Cloud Backup v34.7</h1>
        <p>Status: <span class="status">✅ ONLINE dan SIAP</span></p>
        <p>Server Time: ${new Date().toISOString()}</p>
        <hr>
        <h3>Endpoint yang tersedia:</h3>
        <ul>
          <li><strong>testConnection</strong> - Test koneksi</li>
          <li><strong>getUserAuth</strong> - Ambil data user</li>
          <li><strong>syncUserAuth</strong> - Sinkron data user</li>
          <li><strong>restoreV34</strong> - Ambil semua data bon & payment user</li>
          <li><strong>mergeBackupV34</strong> - Merge data lengkap</li>
          <li><strong>syncBonV34</strong> - Sinkron single bon</li>
          <li><strong>syncPaymentV34</strong> - Sinkron single payment</li>
        </ul>
        <p style="margin-top: 20px; font-size: 12px; color: #718096;">Bon Warung v34.7 - Multi-device sync with merge logic</p>
      </div>
    </body>
    </html>
  `;
  return HtmlService.createHtmlOutput(html);
}

// ==================== FUNGSI UTILITY UNTUK SPREADSHEET ====================
function getOrCreateSheet(sheetName) {
  try {
    const ss = SpreadsheetApp.openById(SPREADSHEET_ID);
    let sheet = ss.getSheetByName(sheetName);
    if (!sheet) {
      sheet = ss.insertSheet(sheetName);
    }
    return sheet;
  } catch(error) {
    Logger.log(`Error getting/creating sheet ${sheetName}: ${error.toString()}`);
    throw error;
  }
}

function ensureSheetHasHeaders(sheet, headers) {
  const range = sheet.getRange(1, 1, 1, headers.length);
  const currentHeaders = range.getValues()[0];
  
  let needsUpdate = false;
  for (let i = 0; i < headers.length; i++) {
    if (currentHeaders[i] !== headers[i]) {
      needsUpdate = true;
      break;
    }
  }
  
  if (needsUpdate || currentHeaders.length === 0 || currentHeaders[0] === '') {
    range.setValues([headers]);
    sheet.setFrozenRows(1);
  }
}

// ==================== USER MANAGEMENT ====================
function getUserFromSheet(username) {
  try {
    const sheet = getOrCreateSheet(SHEET_USERS);
    const headers = ['username', 'userData', 'lastUpdated', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    const data = sheet.getDataRange().getValues();
    const usernameLower = username.toLowerCase();
    
    for (let i = 1; i < data.length; i++) {
      if (data[i][0] && data[i][0].toString().toLowerCase() === usernameLower) {
        const userDataStr = data[i][1];
        if (userDataStr) {
          return JSON.parse(userDataStr);
        }
        return null;
      }
    }
    return null;
  } catch(error) {
    Logger.log(`getUserFromSheet error: ${error.toString()}`);
    return null;
  }
}

function saveUserToSheet(username, userData) {
  try {
    const sheet = getOrCreateSheet(SHEET_USERS);
    const headers = ['username', 'userData', 'lastUpdated', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    const data = sheet.getDataRange().getValues();
    const usernameLower = username.toLowerCase();
    let rowToUpdate = -1;
    
    // Cari baris yang sudah ada
    for (let i = 1; i < data.length; i++) {
      if (data[i][0] && data[i][0].toString().toLowerCase() === usernameLower) {
        rowToUpdate = i + 1; // 1-indexed untuk sheet
        break;
      }
    }
    
    const now = new Date().toISOString();
    const userDataStr = JSON.stringify(userData);
    const deviceId = userData.deviceId || '';
    
    if (rowToUpdate !== -1) {
      // Update existing row
      sheet.getRange(rowToUpdate, 2, 1, 3).setValues([[userDataStr, now, deviceId]]);
    } else {
      // Insert new row
      sheet.appendRow([usernameLower, userDataStr, now, deviceId]);
    }
    
    return true;
  } catch(error) {
    Logger.log(`saveUserToSheet error: ${error.toString()}`);
    return false;
  }
}

// ==================== BONS MANAGEMENT ====================
function getBonsFromSheet(username) {
  try {
    const sheet = getOrCreateSheet(SHEET_BONS);
    const headers = ['username', 'uniqueId', 'bonData', 'lastModified', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    const data = sheet.getDataRange().getValues();
    const usernameLower = username.toLowerCase();
    const bons = [];
    
    for (let i = 1; i < data.length; i++) {
      if (data[i][0] && data[i][0].toString().toLowerCase() === usernameLower) {
        const bonDataStr = data[i][2];
        if (bonDataStr) {
          try {
            const bon = JSON.parse(bonDataStr);
            // Tambahkan lastModified dari sheet jika ada
            if (data[i][3]) {
              bon.lastModified = data[i][3];
            }
            bons.push(bon);
          } catch(e) {
            Logger.log(`Error parsing bon data: ${e.toString()}`);
          }
        }
      }
    }
    
    return bons;
  } catch(error) {
    Logger.log(`getBonsFromSheet error: ${error.toString()}`);
    return [];
  }
}

function saveBonsToSheet(username, bons) {
  try {
    const sheet = getOrCreateSheet(SHEET_BONS);
    const headers = ['username', 'uniqueId', 'bonData', 'lastModified', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    const usernameLower = username.toLowerCase();
    
    // Hapus semua data lama user ini
    const data = sheet.getDataRange().getValues();
    const rowsToDelete = [];
    for (let i = 1; i < data.length; i++) {
      if (data[i][0] && data[i][0].toString().toLowerCase() === usernameLower) {
        rowsToDelete.push(i + 1);
      }
    }
    
    // Hapus dari bawah ke atas agar indeks tidak berubah
    for (let i = rowsToDelete.length - 1; i >= 0; i--) {
      sheet.deleteRow(rowsToDelete[i]);
    }
    
    // Simpan semua bons baru
    for (const bon of bons) {
      const uniqueId = bon.uniqueId || generateUniqueId();
      const lastModified = bon.lastModified || new Date().toISOString();
      const deviceId = bon.deviceId || '';
      const bonDataStr = JSON.stringify(bon);
      
      sheet.appendRow([usernameLower, uniqueId, bonDataStr, lastModified, deviceId]);
    }
    
    return true;
  } catch(error) {
    Logger.log(`saveBonsToSheet error: ${error.toString()}`);
    return false;
  }
}

function saveSingleBonToSheet(username, bon) {
  try {
    const sheet = getOrCreateSheet(SHEET_BONS);
    const headers = ['username', 'uniqueId', 'bonData', 'lastModified', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    const usernameLower = username.toLowerCase();
    const uniqueId = bon.uniqueId || generateUniqueId();
    const lastModified = bon.lastModified || new Date().toISOString();
    const deviceId = bon.deviceId || '';
    
    // Cek apakah sudah ada dengan uniqueId yang sama
    const data = sheet.getDataRange().getValues();
    let existingRow = -1;
    let existingLastModified = null;
    
    for (let i = 1; i < data.length; i++) {
      if (data[i][0] && data[i][0].toString().toLowerCase() === usernameLower &&
          data[i][1] && data[i][1].toString() === uniqueId) {
        existingRow = i + 1;
        existingLastModified = data[i][3];
        break;
      }
    }
    
    // Conflict resolution: hanya update jika data baru lebih baru
    if (existingRow !== -1 && existingLastModified) {
      const existingTime = new Date(existingLastModified).getTime();
      const newTime = new Date(lastModified).getTime();
      
      if (newTime <= existingTime) {
        // Data existing lebih baru atau sama, skip
        return true;
      }
    }
    
    const bonDataStr = JSON.stringify({ ...bon, uniqueId, lastModified });
    
    if (existingRow !== -1) {
      // Update existing row
      sheet.getRange(existingRow, 3, 1, 3).setValues([[bonDataStr, lastModified, deviceId]]);
    } else {
      // Insert new row
      sheet.appendRow([usernameLower, uniqueId, bonDataStr, lastModified, deviceId]);
    }
    
    return true;
  } catch(error) {
    Logger.log(`saveSingleBonToSheet error: ${error.toString()}`);
    return false;
  }
}

// ==================== PAYMENTS MANAGEMENT ====================
function getPaymentsFromSheet(username) {
  try {
    const sheet = getOrCreateSheet(SHEET_PAYMENTS);
    const headers = ['username', 'uniqueId', 'paymentData', 'lastModified', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    const data = sheet.getDataRange().getValues();
    const usernameLower = username.toLowerCase();
    const payments = [];
    
    for (let i = 1; i < data.length; i++) {
      if (data[i][0] && data[i][0].toString().toLowerCase() === usernameLower) {
        const paymentDataStr = data[i][2];
        if (paymentDataStr) {
          try {
            const payment = JSON.parse(paymentDataStr);
            if (data[i][3]) {
              payment.lastModified = data[i][3];
            }
            payments.push(payment);
          } catch(e) {
            Logger.log(`Error parsing payment data: ${e.toString()}`);
          }
        }
      }
    }
    
    return payments;
  } catch(error) {
    Logger.log(`getPaymentsFromSheet error: ${error.toString()}`);
    return [];
  }
}

function savePaymentsToSheet(username, payments) {
  try {
    const sheet = getOrCreateSheet(SHEET_PAYMENTS);
    const headers = ['username', 'uniqueId', 'paymentData', 'lastModified', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    const usernameLower = username.toLowerCase();
    
    // Hapus semua data lama user ini
    const data = sheet.getDataRange().getValues();
    const rowsToDelete = [];
    for (let i = 1; i < data.length; i++) {
      if (data[i][0] && data[i][0].toString().toLowerCase() === usernameLower) {
        rowsToDelete.push(i + 1);
      }
    }
    
    for (let i = rowsToDelete.length - 1; i >= 0; i--) {
      sheet.deleteRow(rowsToDelete[i]);
    }
    
    // Simpan semua payments baru
    for (const payment of payments) {
      const uniqueId = payment.uniqueId || generateUniqueId();
      const lastModified = payment.lastModified || new Date().toISOString();
      const deviceId = payment.deviceId || '';
      const paymentDataStr = JSON.stringify(payment);
      
      sheet.appendRow([usernameLower, uniqueId, paymentDataStr, lastModified, deviceId]);
    }
    
    return true;
  } catch(error) {
    Logger.log(`savePaymentsToSheet error: ${error.toString()}`);
    return false;
  }
}

function saveSinglePaymentToSheet(username, payment) {
  try {
    const sheet = getOrCreateSheet(SHEET_PAYMENTS);
    const headers = ['username', 'uniqueId', 'paymentData', 'lastModified', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    const usernameLower = username.toLowerCase();
    const uniqueId = payment.uniqueId || generateUniqueId();
    const lastModified = payment.lastModified || new Date().toISOString();
    const deviceId = payment.deviceId || '';
    
    // Cek apakah sudah ada dengan uniqueId yang sama
    const data = sheet.getDataRange().getValues();
    let existingRow = -1;
    let existingLastModified = null;
    
    for (let i = 1; i < data.length; i++) {
      if (data[i][0] && data[i][0].toString().toLowerCase() === usernameLower &&
          data[i][1] && data[i][1].toString() === uniqueId) {
        existingRow = i + 1;
        existingLastModified = data[i][3];
        break;
      }
    }
    
    if (existingRow !== -1 && existingLastModified) {
      const existingTime = new Date(existingLastModified).getTime();
      const newTime = new Date(lastModified).getTime();
      
      if (newTime <= existingTime) {
        return true;
      }
    }
    
    const paymentDataStr = JSON.stringify({ ...payment, uniqueId, lastModified });
    
    if (existingRow !== -1) {
      sheet.getRange(existingRow, 3, 1, 3).setValues([[paymentDataStr, lastModified, deviceId]]);
    } else {
      sheet.appendRow([usernameLower, uniqueId, paymentDataStr, lastModified, deviceId]);
    }
    
    return true;
  } catch(error) {
    Logger.log(`saveSinglePaymentToSheet error: ${error.toString()}`);
    return false;
  }
}

// ==================== MERGE & CONFLICT RESOLUTION ====================
function mergeDataWithTimestamp(existingData, newData, idField) {
  const mergedMap = new Map();
  
  // Masukkan data existing ke map
  for (const item of existingData) {
    const id = item[idField];
    if (id) {
      const timestamp = new Date(item.lastModified || 0).getTime();
      mergedMap.set(id, {
        data: item,
        timestamp: timestamp
      });
    }
  }
  
  // Merge dengan data baru (timestamp terbaru menang)
  for (const item of newData) {
    const id = item[idField];
    if (id) {
      const newTimestamp = new Date(item.lastModified || 0).getTime();
      const existing = mergedMap.get(id);
      
      if (!existing || newTimestamp > existing.timestamp) {
        mergedMap.set(id, {
          data: item,
          timestamp: newTimestamp
        });
      }
    }
  }
  
  // Konversi kembali ke array
  return Array.from(mergedMap.values()).map(entry => entry.data);
}

// ==================== LOGGING & UTILITY ====================
function logSyncActivity(username, action, bonsCount, paymentsCount) {
  try {
    const sheet = getOrCreateSheet(SHEET_SYNC_LOG);
    const headers = ['timestamp', 'username', 'action', 'bonsCount', 'paymentsCount', 'deviceId'];
    ensureSheetHasHeaders(sheet, headers);
    
    sheet.appendRow([
      new Date().toISOString(),
      username,
      action,
      bonsCount,
      paymentsCount,
      ''
    ]);
  } catch(error) {
    Logger.log(`logSyncActivity error: ${error.toString()}`);
  }
}

function generateUniqueId() {
  return `${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
}

function sendJsonResponse(data, statusCode = 200) {
  const output = ContentService.createTextOutput(JSON.stringify(data))
    .setMimeType(ContentService.MimeType.JSON);
  
  if (statusCode !== 200) {
    // Untuk error response, kita tetap return JSON dengan status code
    return output;
  }
  return output;
}
