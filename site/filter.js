// Search box on the lookup tables. Without JavaScript the box stays hidden and the full table still shows.
(function () {
  var box = document.querySelector(".filter");
  var input = document.getElementById("filter-input");
  var count = document.getElementById("filter-count");
  var empty = document.getElementById("filter-empty");
  var frame = document.querySelector(".table-scroll.data");
  var rows = Array.prototype.map.call(document.querySelectorAll("#data-table tbody tr"), function (tr) {
    return { tr: tr, text: tr.textContent.toLowerCase() };
  });
  var timer;

  function apply() {
    var words = input.value.toLowerCase().split(/\s+/).filter(Boolean);
    var shown = 0;
    rows.forEach(function (row) {
      var match = words.every(function (w) { return row.text.indexOf(w) !== -1; });
      row.tr.hidden = !match;
      if (match) shown++;
    });
    count.textContent = shown.toLocaleString("en-US") + " of " + rows.length.toLocaleString("en-US") + " rows";
    empty.hidden = shown !== 0;
    frame.hidden = shown === 0;
  }

  box.hidden = false;
  input.addEventListener("input", function () {
    clearTimeout(timer);
    timer = setTimeout(apply, 120);
  });
  apply();
})();
