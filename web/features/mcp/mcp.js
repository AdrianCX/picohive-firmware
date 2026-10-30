document.addEventListener("DOMContentLoaded", function () {
    var url = location.protocol + "//" + location.host + "/mcp";
    var name = (location.hostname || "board").replace(/[^a-zA-Z0-9_-]/g, "-");
    document.getElementById("mcp_url").textContent = url;
    document.getElementById("mcp_cmd").textContent = "claude mcp add --transport http " + name + " " + url;
});
