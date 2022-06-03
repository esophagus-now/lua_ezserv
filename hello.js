var ws = null;

function appendmsg(txt) {
    var t = document.getElementById("msgtable");
    var row = document.createElement("tr");
    row.appendChild(document.createElement("td"));
    row.children[0].innerText = txt;
    t.appendChild(row);
}

function input_submit() {
    var n = document.getElementById("in");
    var txt = n.value;
    n.value = "";
    if (ws !== null) {
        ws.send(txt);
    }
}

function i_am_loaded() {
    var n = document.getElementById("in");
    n.onkeypress = function(e) {
        if (e.key == "Enter") {
            input_submit();
            e.preventDefault();
        }
    };

    ws = new WebSocket("wss://boostbeast.mahkoe.repl.co/mahjong.cgi");
    ws.onmessage = function(e) {
        console.log(e);
        appendmsg(e.data);
    };
}