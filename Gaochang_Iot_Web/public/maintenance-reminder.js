(function(root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.MaintenanceReminder = api;
})(typeof window !== 'undefined' ? window : globalThis, function() {
  // 平台侧计算的当前周期举升数: 累计举升 − 上次保养基准
  function getCycleCount(device) {
    return Math.max(0, (Number(device.total_lift_count) || 0) - (Number(device.last_maintenance_total) || 0));
  }

  function isDue(device) {
    if (!device) return false;
    const threshold = Number(device.maintenance_threshold) || 5000;
    return getCycleCount(device) >= threshold;
  }

  function getDueMaintenanceDevices(devices) {
    return (Array.isArray(devices) ? devices : []).filter((device) => device && device.device_id && isDue(device));
  }

  return { isDue, getDueMaintenanceDevices, getCycleCount };
});
