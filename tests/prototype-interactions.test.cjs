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
  assert.match(html, /:focus-visible\\{outline:2px solid var\\(--amber\\);outline-offset:3px\\}/);
});

test('touch controls expose practical minimum hit areas', () => {
  assert.match(html, /\.tool\{min-height:42px;/);
  assert.match(html, /\.clip\{min-width:42px;min-height:34px;/);
  assert.match(html, /\.band input\{[^}]*min-height:36px;/);
  assert.match(html, /\.pan input\{[^}]*min-height:30px;/);
  assert.match(html, /\.aux input\{[^}]*min-height:38px;/);
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
