.pragma library

// Compact file-size format, 1 decimal for KB/MB, configurable for GB.
function formatSize(bytes, gbPrecision) {
    if (!bytes || bytes < 1024)
        return (bytes || 0) + " B"
    if (bytes < 1048576)
        return (bytes / 1024).toFixed(1) + " KB"
    if (bytes < 1073741824)
        return (bytes / 1048576).toFixed(1) + " MB"
    return (bytes / 1073741824).toFixed(gbPrecision !== undefined ? gbPrecision : 1) + " GB"
}

// Long-form storage format: "1.50 MB", 2 decimals, full unit names. Used for quota display.
function formatStorageSize(bytes) {
    if (bytes === 0)
        return "0 Bytes"
    var k = 1024
    var sizes = ["Bytes", "KB", "MB", "GB", "TB"]
    var i = Math.floor(Math.log(bytes) / Math.log(k))
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i]
}
