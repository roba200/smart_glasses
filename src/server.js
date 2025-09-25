import 'dotenv/config';
import express from 'express';
import morgan from 'morgan';
import { Readable } from 'node:stream';
import Busboy from 'busboy';
import { OpenAI } from 'openai';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const app = express();
const port = process.env.PORT || 3000;
const visionModel = process.env.OPENAI_VISION_MODEL || 'gpt-4o-mini';

function getOpenAI() {
  const apiKey = process.env.OPENAI_API_KEY;
  if (!apiKey) {
    throw new Error('OPENAI_API_KEY is not set');
  }
  return new OpenAI({ apiKey });
}

const replyStyle = process.env.VISION_REPLY_STYLE || 'Respond in one short sentence (max 15 words).';
const defaultPrompt = process.env.VISION_DEFAULT_PROMPT || 'Describe this image.';
const maxTokens = Number.parseInt(process.env.VISION_MAX_TOKENS || '60', 10);
const ttsModel = process.env.OPENAI_TTS_MODEL || 'gpt-4o-mini-tts';

function buildMessages(promptText, dataUrl) {
  const textPart = { type: 'text', text: promptText };
  const imagePart = { type: 'image_url', image_url: { url: dataUrl } };
  return [
    { role: 'system', content: replyStyle },
    { role: 'user', content: [textPart, imagePart] },
  ];
}

// Health
app.get('/health', (_req, res) => {
  res.json({ ok: true, model: visionModel });
});

app.use(morgan('tiny'));

// For small JSON bodies (for alternate base64 JSON mode)
app.use(express.json({ limit: '3mb' }));

// Static test page
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const publicDir = path.join(__dirname, '..', 'public');
app.use(express.static(publicDir));
app.get('/test', (_req, res) => {
  res.sendFile(path.join(publicDir, 'test.html'));
});

// Util: buffer collector
function streamToBuffer(stream) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    stream.on('data', (d) => chunks.push(d));
    stream.on('end', () => resolve(Buffer.concat(chunks)));
    stream.on('error', reject);
  });
}

// Route 1: multipart/form-data upload (field name: image)
app.post('/api/vision/upload', async (req, res) => {
  const bb = new Busboy({ headers: req.headers, limits: { fileSize: 7 * 1024 * 1024 } });
  let fileBuffer = null;
  let fileMimetype = 'image/jpeg';

  bb.on('file', (_name, file, info) => {
    fileMimetype = info?.mimeType || 'image/jpeg';
    streamToBuffer(file)
      .then((buf) => { fileBuffer = buf; })
      .catch((err) => {
        console.error('Buffer error:', err);
        res.status(400).json({ error: 'Failed to read file' });
      });
  });

  bb.on('finish', async () => {
    try {
      if (!fileBuffer) {
        return res.status(400).json({ error: 'No image received' });
      }
      const base64 = fileBuffer.toString('base64');
      const dataUrl = `data:${fileMimetype};base64,${base64}`;

      const openai = getOpenAI();
      const prompt = (req.query?.prompt && String(req.query.prompt)) || defaultPrompt;
      const completion = await openai.chat.completions.create({
        model: visionModel,
        max_tokens: maxTokens,
        messages: buildMessages(prompt, dataUrl),
      });

      const text = completion.choices?.[0]?.message?.content ?? '';
      res.json({ text });
    } catch (err) {
      console.error(err);
      const msg = err?.message?.includes('OPENAI_API_KEY') ? 'Server missing OpenAI API key' : 'Vision processing failed';
      res.status(500).json({ error: msg });
    }
  });

  req.pipe(bb);
});

// Route 2: raw JPEG bytes via POST body
// ESP32-CAM can POST directly with header: Content-Type: image/jpeg
app.post('/api/vision/raw', async (req, res) => {
  try {
    // Guard content-type
    const ct = (req.headers['content-type'] || '').toLowerCase();
    if (!ct.startsWith('image/')) {
      return res.status(415).json({ error: 'Unsupported Media Type' });
    }

    const buf = await streamToBuffer(req);
    if (!buf?.length) return res.status(400).json({ error: 'Empty body' });

    const base64 = buf.toString('base64');
    const dataUrl = `data:${ct};base64,${base64}`;

    const openai = getOpenAI();
    const prompt = (req.query?.prompt && String(req.query.prompt)) || defaultPrompt;
    const completion = await openai.chat.completions.create({
      model: visionModel,
      max_tokens: maxTokens,
      messages: buildMessages(prompt, dataUrl),
    });

    const text = completion.choices?.[0]?.message?.content ?? '';
    res.json({ text });
  } catch (err) {
    console.error(err);
    const msg = err?.message?.includes('OPENAI_API_KEY') ? 'Server missing OpenAI API key' : 'Vision processing failed';
    res.status(500).json({ error: msg });
  }
});

// Route 3: base64 JSON body (fallback)
// {
//   "image_base64": "...",
//   "mime": "image/jpeg",
//   "prompt": "optional prompt"
// }
app.post('/api/vision/base64', async (req, res) => {
  try {
    const { image_base64, mime = 'image/jpeg', prompt } = req.body || {};
    if (!image_base64) return res.status(400).json({ error: 'image_base64 required' });

    const dataUrl = `data:${mime};base64,${image_base64}`;

    const openai = getOpenAI();
    const userPrompt = prompt || defaultPrompt;
    const completion = await openai.chat.completions.create({
      model: visionModel,
      max_tokens: maxTokens,
      messages: buildMessages(userPrompt, dataUrl),
    });

    const text = completion.choices?.[0]?.message?.content ?? '';
    res.json({ text });
  } catch (err) {
    console.error(err);
    const msg = err?.message?.includes('OPENAI_API_KEY') ? 'Server missing OpenAI API key' : 'Vision processing failed';
    res.status(500).json({ error: msg });
  }
});

// Route 4: Text-to-Speech
// Body: { text: string, voice?: string, format?: 'wav' | 'mp3' | 'flac' | 'pcm' }
// Returns audio in requested format (default wav). For ESP32, wav is simplest.
app.post('/api/tts', async (req, res) => {
  try {
    const { text, voice = 'alloy', format = 'wav' } = req.body || {};
    if (!text || typeof text !== 'string') {
      return res.status(400).json({ error: 'text is required' });
    }

    const openai = getOpenAI();
    const ttsResp = await openai.audio.speech.create({
      model: ttsModel,
      voice,
      input: text,
      format,
    });

    const arrayBuffer = await ttsResp.arrayBuffer();
    const buffer = Buffer.from(arrayBuffer);
    const contentType = format === 'wav' ? 'audio/wav' : (format === 'mp3' ? 'audio/mpeg' : 'application/octet-stream');
    res.setHeader('Content-Type', contentType);
    res.setHeader('Content-Length', buffer.length);
    res.setHeader('Connection', 'close');
    return res.status(200).send(buffer);
  } catch (err) {
    console.error(err);
    const msg = err?.message?.includes('OPENAI_API_KEY') ? 'Server missing OpenAI API key' : 'TTS failed';
    res.status(500).json({ error: msg });
  }
});

app.listen(port, () => {
  console.log(`Server running on http://localhost:${port}`);
});
