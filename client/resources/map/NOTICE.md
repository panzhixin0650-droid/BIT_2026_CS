# Offline Shenyang map data

`shenyang.json` contains an OpenStreetMap extract, not generated street artwork.
© OpenStreetMap contributors, licensed under the [Open Database License 1.0](https://opendatacommons.org/licenses/odbl/1-0/).
This extract and adaptations of this database remain available under ODbL 1.0.
See [OpenStreetMap copyright and attribution](https://www.openstreetmap.org/copyright).
The client displays attribution linking to that page. This data license does not
replace the license of the independent Qt renderer/application.

The asset records the source timestamp, SHA-256, endpoint and exact Overpass query.
It covers WGS84 latitude 41.66–41.84, longitude 123.31–123.53, including the two
demo station areas. Coordinates are converted to an approximate GCJ-02 display
alignment with the existing Tencent canvas; no business DTO is converted.
Roads, railways, land use, water polygons (including islands) and names come from
OSM. Display geometry is simplified with a 0.00004-degree (~4 m) tolerance;
sub-pixel polygons may be omitted. Data may be incomplete or outdated; this is
not a routing/traffic database.
Outside the bundled extent, online Tencent mode is needed for detailed mapping.

Refresh explicitly (Node.js 18+ and curl; never part of build/startup):

```bash
node client/tools/prepare_offline_map.mjs --download client/resources/map/shenyang.json
```

Alternatively pass a saved Overpass JSON response instead of `--download`.
No Tencent key, proprietary tiles, user location or account information is used.
The client works fully offline with the bundled extract.

## Coordinate conversion notice

The build-time display conversion is adapted from
[wandergis/coordtransform](https://github.com/wandergis/coordtransform).

The MIT License (MIT)

Copyright (c) 2015 记忆的残骸

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
