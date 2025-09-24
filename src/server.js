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
      const completion = await openai.chat.completions.create({
        model: visionModel,
        messages: [
          {
            role: 'user',
            content: [
              { type: 'text', text: 'Describe this image.' },
              { type: 'image_url', image_url: { url: dataUrl } },
            ],
          },
        ],
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
    const completion = await openai.chat.completions.create({
      model: visionModel,
      messages: [
        {
          role: 'user',
          content: [
            { type: 'text', text: 'Describe this image.' },
            { type: 'image_url', image_url: { url: dataUrl } },
          ],
        },
      ],
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
    const { image_base64, mime = 'image/jpeg', prompt = 'Describe this image.' } = req.body || {};
    if (!image_base64) return res.status(400).json({ error: 'image_base64 required' });

    const dataUrl = `data:${mime};base64,${image_base64}`;

    const openai = getOpenAI();
    const completion = await openai.chat.completions.create({
      model: visionModel,
      messages: [
        {
          role: 'user',
          content: [
            { type: 'text', text: prompt },
            { type: 'image_url', image_url: { url: dataUrl } },
          ],
        },
      ],
    });

    const text = completion.choices?.[0]?.message?.content ?? '';
    res.json({ text });
  } catch (err) {
    console.error(err);
    const msg = err?.message?.includes('OPENAI_API_KEY') ? 'Server missing OpenAI API key' : 'Vision processing failed';
    res.status(500).json({ error: msg });
  }
});

app.listen(port, () => {
  console.log(`Server running on http://localhost:${port}`);
});
