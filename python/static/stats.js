let allloads = {};
var nf = d3.format('.2f')
d3.json('/data').then(json_data => {
    json_data.map(function(row) {
      zdz = d3.zip(row.volts, row.amps).map(
        function(d) {
          return {
            volts: d[0],
            amps: d[1]
          }
        });
      allloads[row.load] = zdz;
    });
    dd = d3.entries(allloads);
    dd.sort((a, b) => (a.key > b.key) ? 1 : -1)
    d3.select('table#table')
      .selectAll('p')
      .data(dd)
      .join(
        enter => {
          tr = enter.append('tr');
          tr.append('td').text(d=>(d.key));
          tr.append('td').text(d=>(nf(d3.mean(d.value.map(r=>r.volts)))));
          tr.append('td').text(d=>(nf(d3.deviation(d.value.map(r=>r.volts)))));
          tr.append('td').text(d=>(nf(d3.mean(d.value.map(r=>r.amps)))));
          tr.append('td').text(d=>(nf(d3.deviation(d.value.map(r=>r.amps)))));
          return tr;
        });
});
