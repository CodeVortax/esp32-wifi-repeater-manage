// Client-side AES-256-GCM authenticated encryption using PBKDF2-HMAC-SHA256.
// Compatible with standard WebCrypto and the ESP32 mbedtls_gcm + mbedtls_pkcs5 implementation.

export interface EncryptedBackupEnvelope {
  format: 'VORTAX_ESP32_BAK_V1';
  kdf: 'PBKDF2-HMAC-SHA256';
  iterations: number;
  cipher: 'AES-256-GCM';
  createdAt: string;
  saltB64: string;
  ivB64: string;
  ciphertextB64: string;
  sha256PlainChecksum: string;
}

const ITERATIONS = 120000;

function toBase64(bytes: Uint8Array): string {
  let binary = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

function fromBase64(b64: string): Uint8Array {
  const binary = atob(b64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) {
    out[i] = binary.charCodeAt(i);
  }
  return out;
}

async function deriveAesKey(password: string, salt: Uint8Array): Promise<CryptoKey> {
  const enc = new TextEncoder();
  const baseKey = await crypto.subtle.importKey(
    'raw',
    enc.encode(password),
    'PBKDF2',
    false,
    ['deriveKey']
  );

  return crypto.subtle.deriveKey(
    {
      name: 'PBKDF2',
      salt: salt.buffer as ArrayBuffer,
      iterations: ITERATIONS,
      hash: 'SHA-256',
    },
    baseKey,
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt']
  );
}

async function sha256Hex(data: Uint8Array): Promise<string> {
  const hash = await crypto.subtle.digest('SHA-256', data.buffer as ArrayBuffer);
  return Array.from(new Uint8Array(hash))
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('');
}

export async function encryptBackupPayload(
  payload: unknown,
  uiPassword: string
): Promise<EncryptedBackupEnvelope> {
  if (!uiPassword || uiPassword.length < 4) {
    throw new Error('Enter a UI password of at least 4 characters to encrypt the backup.');
  }

  const salt = crypto.getRandomValues(new Uint8Array(16));
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const plaintextBytes = new TextEncoder().encode(JSON.stringify(payload, null, 2));
  const checksum = await sha256Hex(plaintextBytes);
  const key = await deriveAesKey(uiPassword, salt);

  const encryptedBuf = await crypto.subtle.encrypt(
    {
      name: 'AES-GCM',
      iv: iv.buffer as ArrayBuffer,
    },
    key,
    plaintextBytes.buffer as ArrayBuffer
  );

  return {
    format: 'VORTAX_ESP32_BAK_V1',
    kdf: 'PBKDF2-HMAC-SHA256',
    iterations: ITERATIONS,
    cipher: 'AES-256-GCM',
    createdAt: new Date().toISOString(),
    saltB64: toBase64(salt),
    ivB64: toBase64(iv),
    ciphertextB64: toBase64(new Uint8Array(encryptedBuf)),
    sha256PlainChecksum: checksum,
  };
}

export async function decryptBackupPayload<T = unknown>(
  envelope: EncryptedBackupEnvelope,
  uiPassword: string
): Promise<{ data: T; checksumVerified: boolean }> {
  if (envelope.format !== 'VORTAX_ESP32_BAK_V1' || envelope.cipher !== 'AES-256-GCM') {
    throw new Error('Unsupported backup envelope format.');
  }
  if (!uiPassword) {
    throw new Error('UI password is required to decrypt this backup.');
  }

  const salt = fromBase64(envelope.saltB64);
  const iv = fromBase64(envelope.ivB64);
  const ciphertext = fromBase64(envelope.ciphertextB64);
  const key = await deriveAesKey(uiPassword, salt);

  let decryptedBuf: ArrayBuffer;
  try {
    decryptedBuf = await crypto.subtle.decrypt(
      {
        name: 'AES-GCM',
        iv: iv.buffer as ArrayBuffer,
      },
      key,
      ciphertext.buffer as ArrayBuffer
    );
  } catch {
    throw new Error('Decryption failed: wrong UI password or tampered backup file.');
  }

  const plainBytes = new Uint8Array(decryptedBuf);
  const actualChecksum = await sha256Hex(plainBytes);
  const parsed = JSON.parse(new TextDecoder().decode(plainBytes)) as T;

  return {
    data: parsed,
    checksumVerified: actualChecksum === envelope.sha256PlainChecksum,
  };
}
