function sendFile() {
    var file = document.getElementById("filename").files[0];
    var reader = new FileReader();
    var rawData = new ArrayBuffer();            
    
    reader.loadend = function() {
    }
    
    reader.onload = function(e) {
        rawData = e.target.result;
        
        // The upgrade command: its path, a NUL, then the binary. Everything
        // after the first NUL is the body, so the NULs in the binary do not matter.
        var blob = new Blob(["/rc/upgrade\0", rawData]);
        handler.send(blob, T("lbl.fwUpload"));
        
        handler.send("/w/", T("lbl.fwFlash"));
    }
    
    reader.readAsArrayBuffer(file);
}
