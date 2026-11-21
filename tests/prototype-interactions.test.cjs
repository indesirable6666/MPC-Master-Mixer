const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { JSDOM } = require('jsdom');

const html = fs.readFileSync('prototype/index.html', 'utf8');

function loadPrototype() {
  return new JSDOM(html, { runScripts: 'dangerously', pretendToBeVisual: true });
}

function input(dom, selector, value) {
  const element = dom.window.document.querySelector(selector);
  assert.ok(element, `control not found: ${selector}`);
  element.value = String(value);
  element.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
  return element;
}

test('top navigation switches Mixer, EQ, AUX and colored VU meter views', () => {
  const dom = loadPrototype();
  const { document } = dom.window;
  const tabs = [...document.querySelectorAll('.view-tab')];
  assert.deepEqual(tabs.map(tab => tab.textContent.trim()), ['MIXER', 'EQ', 'AUX', 'VU MÈTRE']);
  assert.equal(document.body.dataset.view, 'mixer');
  for (const view of ['eq', 'aux', 'meters', 'mixer']) {
    document.querySelector('[data-view="' + view + '"]').click();
    assert.equal(document.body.dataset.view, view);
    for (const tab of tabs) {
      assert.equal(tab.getAttribute('aria-pressed'), String(tab.dataset.view === view));
      assert.equal(tab.classList.contains('active'), tab.dataset.view === view);
    }
  }
  assert.match(html, /body\[data-view="meters"\] \.meter-fill\{background:linear-gradient/);
  assert.ok(document.getElementById('selectionStatus'));
  dom.window.close();
});

test('renders 16 channel strips and one fixed master strip', () => {
  const dom = loadPrototype();
  assert.equal(dom.window.document.querySelectorAll('#strips .channel').length, 16);
  assert.equal(dom.window.document.querySelectorAll('#master .channel').length, 1);
  dom.window.close();
});

test('fader updates its dB and raw-value readouts and selects its channel', () => {
  const dom = loadPrototype();
  const channel = dom.window.document.querySelector('[data-channel="2"]');
  input(dom, '[data-channel="2"] [data-volume]', 50);
  assert.equal(channel.querySelector('.db').textContent, '-6.0 dB');
  assert.equal(channel.querySelector('.value').textContent, '50 / 127');
  assert.equal(channel.querySelector('[data-volume]').getAttribute('aria-valuetext'), '-6.0 dB, 50 sur 127');
  input(dom, '[data-channel="2"] [data-volume]', 0);
  assert.equal(channel.querySelector('.db').textContent, '−∞ dB');
  assert.equal(channel.querySelector('[data-volume]').getAttribute('aria-valuetext'), '−∞ dB, 0 sur 127');
  input(dom, '[data-channel="2"] [data-volume]', 127);
  assert.equal(channel.querySelector('.db').textContent, '2.1 dB');
  assert.equal(channel.querySelector('[data-volume]').getAttribute('aria-valuetext'), '2.1 dB, 127 sur 127');
  assert.ok(channel.classList.contains('selected'));
  dom.window.close();
});

test('pan readout reports left, center and right positions', () => {
  const dom = loadPrototype();
  const pan = '[data-channel="0"] [data-pan]';
  input(dom, pan, -35);
  assert.equal(dom.window.document.querySelector('[data-channel="0"] .pan-value').textContent, 'L 35');
  assert.equal(dom.window.document.querySelector(pan).title, 'Pan L 35');
  input(dom, pan, 0);
  assert.equal(dom.window.document.querySelector('[data-channel="0"] .pan-value').textContent, 'C');
  input(dom, pan, 42);
  assert.equal(dom.window.document.querySelector('[data-channel="0"] .pan-value').textContent, 'R 42');
  dom.window.close();
});

test('mute and solo buttons toggle their visual and accessible states', () => {
  const dom = loadPrototype();
  const channel = dom.window.document.querySelector('[data-channel="3"]');
  for (const name of ['mute', 'solo']) {
    const button = channel.querySelector(`[data-toggle="${name}"]`);
    button.click();
    assert.equal(button.getAttribute('aria-pressed'), 'true');
    assert.ok(button.classList.contains('active'));
    button.click();
    assert.equal(button.getAttribute('aria-pressed'), 'false');
    assert.ok(!button.classList.contains('active'));
  }
  assert.ok(channel.classList.contains('selected'));
  dom.window.close();
});

test('EQ and aux controls update their displayed values', () => {
  const dom = loadPrototype();
  const channel = '[data-channel="4"]';
  input(dom, `${channel} [data-eq="2"]`, 3.5);
  input(dom, `${channel} [data-aux="1"]`, 64);
  const eq = dom.window.document.querySelector(`${channel} [data-eq="2"]`);
  const aux = dom.window.document.querySelector(`${channel} [data-aux="1"]`);
  assert.equal(eq.nextElementSibling.textContent, '+3.5 dB');
  assert.equal(eq.getAttribute('aria-valuetext'), '+3.5 décibels');
  assert.equal(aux.nextElementSibling.textContent, '64%');
  assert.equal(aux.getAttribute('aria-valuetext'), '64 pour cent');
  dom.window.close();
});

test('reset restores fader, pan, EQ, aux, mute and solo defaults', () => {
  const dom = loadPrototype();
  const channel = '[data-channel="5"]';
  input(dom, `${channel} [data-volume]`, 40);
  input(dom, `${channel} [data-pan]`, -70);
  input(dom, `${channel} [data-eq="1"]`, 5);
  input(dom, `${channel} [data-aux="2"]`, 88);
  dom.window.document.querySelector(`${channel} [data-toggle="mute"]`).click();
  dom.window.document.querySelector(`${channel} [data-toggle="solo"]`).click();
  dom.window.document.getElementById('reset').click();
  const el = dom.window.document.querySelector(channel);
  assert.equal(el.querySelector('[data-volume]').value, '100');
  assert.equal(el.querySelector('[data-pan]').value, '0');
  assert.equal(el.querySelector('[data-eq="1"]').value, '0');
  assert.equal(el.querySelector('[data-aux="2"]').value, '0');
  assert.equal(el.querySelector('.pan-value').textContent, 'C');
  assert.equal(el.querySelector('[data-pan]').title, 'Pan C');
  assert.equal(el.querySelector('[data-toggle="mute"]').getAttribute('aria-pressed'), 'false');
  assert.equal(el.querySelector('[data-toggle="solo"]').getAttribute('aria-pressed'), 'false');
  dom.window.close();
});

test('meter animation is opt-in and stops when switched off', () => {
  const dom = loadPrototype();
  const button = dom.window.document.getElementById('meterDemo');
  assert.equal(button.getAttribute('aria-pressed'), 'false');
  assert.equal(button.textContent, 'DÉMO MÈTRES : OFF');
  button.click();
  assert.equal(button.getAttribute('aria-pressed'), 'true');
  assert.equal(button.textContent, 'DÉMO MÈTRES : ON');
  button.click();
  assert.equal(button.getAttribute('aria-pressed'), 'false');
  assert.equal(button.textContent, 'DÉMO MÈTRES : OFF');
  assert.equal(dom.window.document.querySelector('[data-channel="0"] [data-meter-value="peak"]').textContent, '0%');
  dom.window.close();
});

test('all interactive controls expose a consistent visible keyboard focus ring', () => {
  assert.match(html, /:focus-visible\{outline:2px solid var\(--amber\);outline-offset:3px\}/);
});

test('touch controls expose practical minimum hit areas', () => {
  assert.match(html, /\.tool\{min-height:42px;/);
  assert.match(html, /\.clip\{min-width:42px;min-height:34px;/);
  assert.match(html, /\.band input\{[^}]*min-height:36px;/);
  assert.match(html, /\.pan input\{[^}]*min-height:40px;/);
  assert.match(html, /\.aux input\{[^}]*min-height:40px;/);
  assert.match(html, /\.toggle\{flex:1;min-height:40px;/);
});

test('bank navigation switches between channels 1–8 and 9–16', () => {
  const dom = loadPrototype();
  const prev = dom.window.document.getElementById('prev');
  const next = dom.window.document.getElementById('next');
  const status = dom.window.document.getElementById('bankStatus');
  assert.equal(status.textContent, 'BANQUE 1/2 · PISTES 1–8');
  assert.equal(prev.disabled, true);
  assert.equal(next.disabled, false);
  next.click();
  assert.equal(status.textContent, 'BANQUE 2/2 · PISTES 9–16');
  assert.equal(prev.disabled, false);
  assert.equal(next.disabled, true);
  assert.equal(status.getAttribute('aria-label'), 'Banque 2 sur 2, pistes 9 à 16');
  prev.click();
  assert.equal(status.textContent, 'BANQUE 1/2 · PISTES 1–8');
  assert.equal(prev.disabled, true);
  assert.equal(next.disabled, false);
  dom.window.close();
});

test('peak and RMS meters expose accessible percentage values', () => {
  const dom = loadPrototype();
  for (const channel of dom.window.document.querySelectorAll('.channel')) {
    const peak = channel.querySelector('[data-meter="peak"]').parentElement;
    const rms = channel.querySelector('[data-meter="rms"]').parentElement;
    for (const [meter, label] of [[peak, 'Peak simulé'], [rms, 'RMS simulé']]) {
      assert.equal(meter.getAttribute('role'), 'meter');
      assert.equal(meter.getAttribute('aria-label'), label);
      assert.equal(meter.getAttribute('aria-valuemin'), '0');
      assert.equal(meter.getAttribute('aria-valuemax'), '100');
      assert.equal(meter.getAttribute('aria-valuenow'), '0');
      assert.equal(meter.getAttribute('aria-valuetext'), '0 pour cent');
    }
    assert.equal(channel.querySelector('[data-meter-hold]').getAttribute('aria-hidden'), 'true');
  }
  dom.window.close();
});

test('live selection and bank announcements expose atomic status semantics and meters are named groups', () => {
  const dom = loadPrototype();
  const selection = dom.window.document.getElementById('selectionStatus');
  const bank = dom.window.document.getElementById('bankStatus');
  assert.equal(selection.getAttribute('role'), 'status');
  assert.equal(selection.getAttribute('aria-live'), 'polite');
  assert.equal(selection.getAttribute('aria-atomic'), 'true');
  assert.equal(bank.getAttribute('role'), 'status');
  assert.equal(bank.getAttribute('aria-atomic'), 'true');
  for (const meter of dom.window.document.querySelectorAll('.meter-panel')) {
    assert.equal(meter.getAttribute('role'), 'group');
    assert.equal(meter.getAttribute('aria-label'), 'Mesures simulées');
  }
  dom.window.close();
});

test('bank navigation buttons expose clear accessible names and controlled regions', () => {
  const dom = loadPrototype();
  const prev = dom.window.document.getElementById('prev');
  const next = dom.window.document.getElementById('next');
  assert.equal(prev.getAttribute('aria-label'), 'Afficher la banque précédente');
  assert.equal(next.getAttribute('aria-label'), 'Afficher la banque suivante');
  for (const button of [prev, next]) {
    assert.equal(button.getAttribute('aria-controls'), 'scrollArea bankStatus');
  }
  dom.window.close();
});

test('bank indicator follows horizontal touch scrolling', () => {
  const dom = loadPrototype();
  const area = dom.window.document.getElementById('scrollArea');
  const secondBankTrack = dom.window.document.querySelector('[data-channel="8"]');
  Object.defineProperty(secondBankTrack, 'offsetLeft', { configurable: true, value: 900 });
  area.scrollLeft = 901;
  area.dispatchEvent(new dom.window.Event('scroll'));
  assert.equal(dom.window.document.getElementById('bankStatus').textContent, 'BANQUE 2/2 · PISTES 9–16');
  assert.equal(dom.window.document.getElementById('next').disabled, true);
  area.scrollLeft = 0;
  area.dispatchEvent(new dom.window.Event('scroll'));
  assert.equal(dom.window.document.getElementById('bankStatus').textContent, 'BANQUE 1/2 · PISTES 1–8');
  assert.equal(dom.window.document.getElementById('prev').disabled, true);
  dom.window.close();
});

test('selected track is announced and marked accessibly', () => {
  const dom = loadPrototype();
  const first = dom.window.document.querySelector('[data-channel="0"]');
  const status = dom.window.document.getElementById('selectionStatus');
  assert.equal(first.getAttribute('aria-current'), 'true');
  assert.equal(status.textContent, 'PISTE 01 · KICK');
  const fourth = dom.window.document.querySelector('[data-channel="3"]');
  fourth.focus();
  assert.equal(first.hasAttribute('aria-current'), false);
  assert.equal(fourth.getAttribute('aria-current'), 'true');
  assert.equal(status.textContent, 'PISTE 04 · PERC');
  fourth.querySelector('.channel-title').textContent = 'BASSE SYNTHÉTIQUE TRÈS LONGUE';
  fourth.blur();
  fourth.focus();
  assert.equal(status.textContent, 'PISTE 04 · BASSE SYNTHÉTIQUE TRÈS LONGUE');
  assert.equal(fourth.querySelector('.channel-title').title, 'BASSE SYNTHÉTIQUE TRÈS LONGUE');
  assert.match(html, /\.channel-title\{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap/);
  assert.match(html, /\.channel-type\{flex-shrink:0\}/);
  dom.window.close();
});

test('left and right arrow keys move focus between adjacent channels', () => {
  const dom = loadPrototype();
  const third = dom.window.document.querySelector('[data-channel="2"]');
  third.focus();
  third.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true }));
  assert.equal(dom.window.document.activeElement.dataset.channel, '3');
  dom.window.document.activeElement.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'ArrowLeft', bubbles: true }));
  assert.equal(dom.window.document.activeElement.dataset.channel, '2');
  const first = dom.window.document.querySelector('[data-channel="0"]');
  first.focus();
  first.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'ArrowLeft', bubbles: true }));
  assert.equal(dom.window.document.activeElement.dataset.channel, '0');
  dom.window.close();
});

test('Home and End keys jump to the first and last channel strips', () => {
  const dom = loadPrototype();
  const middle = dom.window.document.querySelector('[data-channel="7"]');
  middle.focus();
  middle.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'Home', bubbles: true }));
  assert.equal(dom.window.document.activeElement.dataset.channel, '0');
  assert.equal(dom.window.document.getElementById('selectionStatus').textContent, 'PISTE 01 · KICK');
  assert.equal(dom.window.document.getElementById('bankStatus').textContent, 'BANQUE 1/2 · PISTES 1–8');
  dom.window.document.activeElement.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'End', bubbles: true }));
  assert.equal(dom.window.document.activeElement.dataset.channel, '15');
  assert.equal(dom.window.document.getElementById('selectionStatus').textContent, 'PISTE 16 · AUDIO 3');
  assert.equal(dom.window.document.getElementById('bankStatus').textContent, 'BANQUE 2/2 · PISTES 9–16');
  dom.window.close();
});

test('PageUp and PageDown move by one eight-channel bank', () => {
  const dom = loadPrototype();
  const fourth = dom.window.document.querySelector('[data-channel="3"]');
  fourth.focus();
  fourth.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'PageDown', bubbles: true }));
  assert.equal(dom.window.document.activeElement.dataset.channel, '11');
  assert.equal(dom.window.document.getElementById('bankStatus').textContent, 'BANQUE 2/2 · PISTES 9–16');
  dom.window.document.activeElement.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'PageUp', bubbles: true }));
  assert.equal(dom.window.document.activeElement.dataset.channel, '3');
  assert.equal(dom.window.document.getElementById('bankStatus').textContent, 'BANQUE 1/2 · PISTES 1–8');
  dom.window.close();
});

test('clicking any channel action also selects that channel', () => {
  const dom = loadPrototype();
  const channel = dom.window.document.querySelector('[data-channel="6"]');
  const status = dom.window.document.getElementById('selectionStatus');
  channel.querySelector('.fx').click();
  assert.equal(status.textContent, 'PISTE 07 · SYNTH 2');
  assert.equal(channel.getAttribute('aria-current'), 'true');
  channel.querySelector('[data-clip]').click();
  assert.equal(status.textContent, 'PISTE 07 · SYNTH 2');
  channel.querySelector('[data-toggle="mute"]').click();
  assert.equal(status.textContent, 'PISTE 07 · SYNTH 2');
  dom.window.close();
});

test('reduced-motion preference disables smooth scrolling and decorative transitions', () => {
  assert.match(html, /@media\(prefers-reduced-motion:reduce\)\{\.scroll-area\{scroll-behavior:auto\}\.channel,\.meter-fill\{transition:none\}\}/);
});

test('narrow-screen layout compacts the toolbar and footer without shrinking channel controls', () => {
  assert.match(html, /@media\(max-width:480px\)\{[^}]*\.topbar/);
  assert.match(html, /\.topbar>\.mode\{display:none\}/);
  assert.match(html, /\.bank-status\{max-width:72px/);
  assert.match(html, /\.bottom \.hint\{display:none\}/);
  assert.match(html, /\.channel\{width:168px;min-width:168px\}/);
});

test('Audio In 1 and 2 can be linked as a stereo pair with shared controls', () => {
  const dom = loadPrototype();
  const button = dom.window.document.getElementById('stereoLink');
  const left = dom.window.document.getElementById('audioIn1');
  const right = dom.window.document.getElementById('audioIn2');
  assert.ok(left && right);
  assert.equal(button.getAttribute('aria-pressed'), 'false');
  button.click();
  assert.equal(button.getAttribute('aria-pressed'), 'true');
  assert.match(button.textContent, /ON/);
  assert.ok(left.classList.contains('stereo-linked'));
  assert.ok(right.classList.contains('stereo-linked'));
  assert.equal(left.querySelector('.channel-type').textContent, 'STÉRÉO · L');
  assert.equal(right.querySelector('.channel-type').textContent, 'STÉRÉO · R');
  assert.equal(left.querySelector('[data-pan]').disabled, true);
  assert.equal(right.querySelector('[data-pan]').disabled, true);
  const leftBalance = left.querySelector('[data-stereo-balance]');
  const rightBalance = right.querySelector('[data-stereo-balance]');
  assert.ok(leftBalance && rightBalance);
  assert.equal(leftBalance.disabled, false);
  assert.equal(rightBalance.disabled, false);
  leftBalance.value = '-35';
  leftBalance.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
  assert.equal(rightBalance.value, '-35');
  assert.equal(right.querySelector('.balance-value').textContent, 'L 35');

  const leftFader = left.querySelector('[data-volume]');
  const rightFader = right.querySelector('[data-volume]');
  leftFader.value = '73';
  leftFader.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
  assert.equal(rightFader.value, '73');
  assert.equal(right.querySelector('.value').textContent, '73 / 127');

  const leftAux = left.querySelector('[data-aux="2"]');
  const rightAux = right.querySelector('[data-aux="2"]');
  leftAux.value = '45';
  leftAux.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
  assert.equal(rightAux.value, '45');
  assert.equal(rightAux.nextElementSibling.textContent, '45%');

  left.querySelector('[data-toggle="mute"]').click();
  assert.equal(right.querySelector('[data-toggle="mute"]').getAttribute('aria-pressed'), 'true');

  button.click();
  assert.equal(button.getAttribute('aria-pressed'), 'false');
  assert.ok(!left.classList.contains('stereo-linked'));
  assert.ok(!right.classList.contains('stereo-linked'));
  assert.equal(left.querySelector('[data-pan]').disabled, false);
  assert.equal(right.querySelector('[data-pan]').disabled, false);
  assert.equal(leftBalance.disabled, true);
  assert.equal(rightBalance.disabled, true);
  dom.window.close();
});



test('host snapshot event updates all 16 tracks and Master without claiming live capture', () => {
  const dom = loadPrototype();
  const { window } = dom;
  const button = window.document.getElementById('meterDemo');
  button.click();
  assert.equal(button.getAttribute('aria-pressed'), 'true');

  const channels = Array.from({ length: 17 }, (_, index) => ({
    peak: index === 16 ? 0.999 : (index + 1) / 100,
    rms: index === 16 ? 0.5 : (index + 1) / 200
  }));
  window.dispatchEvent(new window.CustomEvent('mpc-meter-snapshot', {
    detail: { sequence: 10, channels }
  }));

  assert.equal(button.getAttribute('aria-pressed'), 'false');
  for (let index = 0; index < 16; index++) {
    const strip = window.document.querySelector('[data-channel="' + index + '"]');
    assert.equal(strip.querySelector('[data-meter-value="peak"]').textContent,
      Math.round(channels[index].peak * 100) + '%');
    assert.equal(strip.querySelector('[data-meter-value="rms"]').textContent,
      Math.round(channels[index].rms * 100) + '%');
  }
  const master = window.document.querySelector('[data-channel="master"]');
  assert.equal(master.querySelector('[data-meter-value="peak"]').textContent, '100%');
  assert.equal(master.querySelector('[data-clip]').textContent, 'CLIP');

  // Stale and malformed snapshots are ignored rather than rolling UI state back.
  window.dispatchEvent(new window.CustomEvent('mpc-meter-snapshot', {
    detail: { sequence: 9, channels: channels.map(() => ({ peak: 0, rms: 0 })) }
  }));
  window.dispatchEvent(new window.CustomEvent('mpc-meter-snapshot', {
    detail: { sequence: 11, channels: channels.slice(0, 16) }
  }));
  assert.equal(master.querySelector('[data-meter-value="peak"]').textContent, '100%');
  dom.window.close();
});


test('browser audio demo generates a local tone controlled by selected track and master', () => {
  class FakeAudioNode {
    constructor() { this.gain = { value: 1 }; this.pan = { value: 0 }; this.frequency = { value: 0 }; this.connections = []; }
    connect(node) { this.connections.push(node); }
  }
  class FakeAudioContext {
    constructor() { this.state = 'running'; this.destination = new FakeAudioNode(); this.closed = false; FakeAudioContext.last = this; }
    createOscillator() { this.oscillator = new FakeAudioNode(); this.oscillator.start = () => { this.oscillator.started = true; }; this.oscillator.stop = () => { this.oscillator.stopped = true; }; return this.oscillator; }
    createGain() { const node = new FakeAudioNode(); this.gains = this.gains || []; this.gains.push(node); return node; }
    createStereoPanner() { this.panner = new FakeAudioNode(); return this.panner; }
    createAnalyser() { this.analyser = new FakeAudioNode(); this.analyser.getFloatTimeDomainData = buffer => { for (let i=0;i<buffer.length;i++) buffer[i]=i%2===0?0.5:-0.5; }; return this.analyser; }
    close() { this.closed = true; this.state = 'closed'; return Promise.resolve(); }
    resume() { this.state = 'running'; return Promise.resolve(); }
  }
  const dom = new JSDOM(html, {
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    beforeParse(window) { window.AudioContext = FakeAudioContext; }
  });
  const { document } = dom.window;
  const button = document.getElementById('audioDemo');
  button.click();
  assert.equal(button.getAttribute('aria-pressed'), 'true');
  assert.match(button.textContent, /220 Hz/);
  const context = FakeAudioContext.last;
  assert.equal(context.oscillator.frequency.value, 220);
  assert.equal(context.gains[0].gain.value, 0.15);
  assert.equal(context.gains[1].gain.value, 1);
  const selectedFader = document.querySelector('[data-channel="0"] [data-volume]');
  selectedFader.value = '50';
  selectedFader.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
  assert.equal(context.gains[0].gain.value, 0.075);
  const selectedPan = document.querySelector('[data-channel="0"] [data-pan]');
  selectedPan.value = '35';
  selectedPan.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
  assert.equal(context.panner.pan.value, 0.35);
  const mute = document.querySelector('[data-channel="0"] [data-toggle="mute"]');
  mute.click();
  assert.equal(context.gains[0].gain.value, 0);
  mute.click();
  const masterFader = document.querySelector('[data-channel="master"] [data-volume]');
  masterFader.value = '60';
  masterFader.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
  assert.equal(context.gains[1].gain.value, 0.6);
  assert.equal(typeof dom.window.updateBrowserAudioMeters, 'function');
  dom.window.updateBrowserAudioMeters();
  assert.equal(document.querySelector('[data-channel="0"] [data-meter-value="peak"]').textContent, '50%');
  assert.equal(document.querySelector('[data-channel="0"] [data-meter-value="rms"]').textContent, '50%');
  assert.equal(document.querySelector('[data-channel="master"] [data-meter-value="peak"]').textContent, '30%');
  assert.equal(document.querySelector('[data-channel="master"] [data-meter-value="rms"]').textContent, '30%');
  document.querySelector('[data-channel="master"] [data-toggle="mute"]').click();
  assert.equal(context.gains[1].gain.value, 0);
  dom.window.updateBrowserAudioMeters();
  assert.equal(document.querySelector('[data-channel="master"] [data-meter-value="peak"]').textContent, '0%');
  button.click();
  assert.equal(context.oscillator.stopped, true);
  assert.equal(context.closed, true);
  assert.equal(button.getAttribute('aria-pressed'), 'false');
  dom.window.close();
});

test('audio demo button clearly reports when browser Web Audio is unavailable', () => {
  const dom = loadPrototype();
  const button = dom.window.document.getElementById('audioDemo');
  button.click();
  assert.equal(button.getAttribute('aria-pressed'), 'false');
  assert.equal(button.textContent, 'AUDIO INDISPONIBLE');
  dom.window.close();
});

test('shared audio reference fixture defines browser/native equal-power expectations', () => {
  const fixture = fs.readFileSync('tests/fixtures/audio_signal_reference.csv', 'utf8').trim().split(/\r?\n/);
  assert.equal(fixture[0], 'scenario,track_gain,pan,master_gain,track_mute,master_mute,expected_left_gain,expected_right_gain,expected_master_peak');
  const scenarios = fixture.slice(1).map(line => {
    const [name, trackGain, pan, masterGain, trackMute, masterMute,
      expectedLeft, expectedRight, expectedPeak] = line.split(',');
    const gain = Number(trackGain);
    const position = Number(pan);
    const master = Number(masterGain);
    const silent = Number(trackMute) !== 0 || Number(masterMute) !== 0;
    const left = silent ? 0 : gain * Math.sqrt((1 - position) / 2) * master;
    const right = silent ? 0 : gain * Math.sqrt((1 + position) / 2) * master;
    const peak = 0.5 * Math.max(left, right);
    return { name, left, right, peak, expectedLeft: Number(expectedLeft),
      expectedRight: Number(expectedRight), expectedPeak: Number(expectedPeak) };
  });
  assert.equal(scenarios.length, 5);
  for (const scenario of scenarios) {
    assert.ok(Math.abs(scenario.left - scenario.expectedLeft) < 0.00001, scenario.name + ' left gain');
    assert.ok(Math.abs(scenario.right - scenario.expectedRight) < 0.00001, scenario.name + ' right gain');
    assert.ok(Math.abs(scenario.peak - scenario.expectedPeak) < 0.00001, scenario.name + ' master peak');
  }
});
