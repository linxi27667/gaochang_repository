function nowISO(date = new Date()) {
  const beijingOffsetMs = 8 * 60 * 60 * 1000;
  return new Date(date.getTime() + beijingOffsetMs).toISOString().replace('T', ' ').substring(0, 19);
}

module.exports = { nowISO };
