// Developer-only, explicit refresh. Never run by CMake or the client at startup.
// Usage: node client/tools/prepare_offline_map.mjs input-overpass.json output.json
// Fetch input with the bounded query below, or pass --download as the first argument.
import fs from 'node:fs';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';

const endpoint = 'https://overpass-api.de/api/interpreter';
const bbox = [41.66, 123.31, 41.84, 123.53]; // south, west, north, east; WGS84
const query = `[out:json][timeout:45][bbox:${bbox.join(',')}];(way[highway][highway!~"^(footway|path|steps|cycleway|construction|proposed|track)$"];way[railway=rail];way[waterway=river];way[natural=water];relation[natural=water];way[leisure~"^(park|garden|golf_course|pitch)$"];relation[leisure=park];way[landuse~"^(residential|commercial|retail|industrial|forest|grass)$"];);out geom;`;

// WGS84 -> GCJ-02 display alignment, adapted from wandergis/coordtransform
// (MIT, Copyright (c) 2015 记忆的残骸). Full notice: ../resources/map/NOTICE.md.
// Only the offline background is converted; API/DTO coordinates are untouched.
function displayCoordinate({lon: lng, lat}) {
  const pi = Math.PI, x = lng - 105, y = lat - 35;
  const common = (20 * Math.sin(6 * x * pi) + 20 * Math.sin(2 * x * pi)) * 2 / 3;
  let dy = -100 + 2*x + 3*y + .2*y*y + .1*x*y + .2*Math.sqrt(Math.abs(x)) + common;
  dy += (20*Math.sin(y*pi) + 40*Math.sin(y*pi/3)) * 2/3;
  dy += (160*Math.sin(y*pi/12) + 320*Math.sin(y*pi/30)) * 2/3;
  let dx = 300 + x + 2*y + .1*x*x + .1*x*y + .1*Math.sqrt(Math.abs(x)) + common;
  dx += (20*Math.sin(x*pi) + 40*Math.sin(x*pi/3)) * 2/3;
  dx += (150*Math.sin(x*pi/12) + 300*Math.sin(x*pi/30)) * 2/3;
  const rad = lat*pi/180, ee = .00669342162296594323, a = 6378245;
  const magic = 1 - ee*Math.sin(rad)**2, root = Math.sqrt(magic);
  return [lng + dx*180/(a/root*Math.cos(rad)*pi),
          lat + dy*180/(a*(1-ee)/(magic*root)*pi)].map(v => +v.toFixed(6));
}

// Join multipolygon way members by their actual shared end nodes. Never close
// incomplete ways across a river, and preserve inner rings (islands/holes).
function rings(members) {
  const remaining = members.filter(m => m.type === 'way' && m.geometry?.length > 1)
    .map(m => m.geometry.slice());
  const equal = (a,b) => a.lat === b.lat && a.lon === b.lon;
  const result = [];
  while (remaining.length) {
    const ring = remaining.pop();
    while (!equal(ring[0], ring.at(-1))) {
      const i = remaining.findIndex(p => equal(p[0], ring.at(-1)) || equal(p.at(-1), ring.at(-1)));
      if (i < 0) break;
      const next = remaining.splice(i,1)[0];
      if (!equal(next[0], ring.at(-1))) next.reverse();
      ring.push(...next.slice(1));
    }
    if (equal(ring[0], ring.at(-1))) result.push(ring);
  }
  return result;
}

function kind(tags) {
  if (tags.natural === 'water') return 'water';
  if (tags.waterway === 'river') return 'river';
  if (tags.leisure || ['forest','grass'].includes(tags.landuse)) return 'park';
  if (tags.landuse) return tags.landuse === 'industrial' ? 'industrial' : 'urban';
  if (tags.railway === 'rail') return 'rail';
  if (/^(motorway|trunk)/.test(tags.highway)) return 'highway';
  if (/^(primary|secondary)/.test(tags.highway)) return 'major';
  if (/^tertiary/.test(tags.highway)) return 'street';
  return tags.highway ? 'local' : null;
}

// Display-only Douglas-Peucker simplification (~4 m). Retain the actual road
// shape without thousands of sub-pixel survey vertices in the offline canvas.
function simplify(points) {
  if (points.length <= 2) return points;
  const first = points[0], last = points.at(-1), dx = last[0]-first[0], dy = last[1]-first[1];
  let maximum = .00004**2, index = -1;
  for (let i=1; i<points.length-1; ++i) {
    const point = points[i], length = dx*dx+dy*dy;
    const t = length ? Math.max(0, Math.min(1, ((point[0]-first[0])*dx+(point[1]-first[1])*dy)/length)) : 0;
    const distance = (point[0]-first[0]-t*dx)**2 + (point[1]-first[1]-t*dy)**2;
    if (distance > maximum) { maximum = distance; index = i; }
  }
  return index < 0 ? [first,last] : [...simplify(points.slice(0,index+1)).slice(0,-1), ...simplify(points.slice(index))];
}

const [input, output] = process.argv.slice(2);
if (!input || !output) throw new Error('Expected input-overpass.json (or --download) and output.json');
const raw = input === '--download' ? execFileSync('curl', ['--fail','--silent','--show-error',
  '--max-time','55','--data-urlencode',`data=${query}`,'--user-agent',
  'BIT-Charge-Demo/1.0 (+https://github.com/panzhixin0650-droid/BIT_2026_CS)', endpoint],
  {maxBuffer: 32*1024*1024}) : fs.readFileSync(input);
const data = JSON.parse(raw);
if (data.remark || !data.osm3s?.timestamp_osm_base || !Array.isArray(data.elements))
  throw new Error('Incomplete/invalid Overpass response; existing asset is unchanged');
const features = [];
for (const element of data.elements) {
  const tags = element.tags || {}, type = kind(tags);
  if (!type) continue;
  const area = ['water','park','urban','industrial'].includes(type);
  const paths = element.type === 'relation'
    ? [...rings(element.members.filter(m => m.role !== 'inner')),
       ...rings(element.members.filter(m => m.role === 'inner'))]
    : element.geometry ? [element.geometry] : [];
  const valid = paths.filter(p => p.length >= (area ? 4 : 2) && p.every(v => Number.isFinite(v.lat) && Number.isFinite(v.lon))
    && (!area || (p[0].lat === p.at(-1).lat && p[0].lon === p.at(-1).lon)));
  if (!valid.length) continue;
  const simplified = valid.map(path => simplify(path.map(displayCoordinate))).filter(path => path.length >= (area ? 4 : 2));
  if (simplified.length) features.push({id: `${element.type}/${element.id}`, kind: type,
    name: tags['name:zh'] || tags.name || '', paths: simplified});
}
if (features.length < 1000 || !features.some(f => f.kind === 'water'))
  throw new Error('Extract lacks roads/water; existing asset is unchanged');
const metadata = {version: 1, source: 'OpenStreetMap contributors', license: 'ODbL-1.0',
  attributionUrl: 'https://www.openstreetmap.org/copyright', endpoint, query, bbox,
  sourceTimestamp: data.osm3s.timestamp_osm_base,
  sourceSha256: crypto.createHash('sha256').update(raw).digest('hex'),
  coordinates: 'GCJ-02 approximation for display; source WGS84', features};
// One feature per line keeps the generated extract diffable. No imagery or key.
const contents = JSON.stringify(metadata).replace('"features":[', '"features":[\n')
  .replaceAll('},{"id":', '},\n{"id":') + '\n';
fs.writeFileSync(output, contents);
console.log(`${features.length} real map features; ${Buffer.byteLength(contents)} bytes; ${metadata.sourceTimestamp}`);
