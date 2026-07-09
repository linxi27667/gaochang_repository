// IP限流中间件 - 内存Map存储，重启清零
const requests = new Map();

// 定期清理过期记录（每5分钟）
setInterval(() => {
  const now = Date.now();
  for (const [key, data] of requests) {
    if (now - data.start > data.windowMs) requests.delete(key);
  }
}, 5 * 60 * 1000).unref();

function rateLimit({ windowMs = 60000, max = 3 } = {}) {
  return (req, res, next) => {
    const ip = req.ip || req.connection.remoteAddress || 'unknown';
    const key = `${ip}:${req.baseUrl}${req.path}`;
    const now = Date.now();

    let record = requests.get(key);
    if (!record || now - record.start > windowMs) {
      record = { start: now, count: 0, windowMs };
      requests.set(key, record);
    }

    record.count++;
    if (record.count > max) {
      return res.status(429).json({ error: '请求过于频繁，请稍后再试' });
    }
    next();
  };
}

module.exports = { rateLimit };
