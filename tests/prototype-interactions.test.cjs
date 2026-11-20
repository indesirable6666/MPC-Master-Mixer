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
  assert.equal(dom.window.document.querySelector(`${channel} [data-eq="2"] + output`).textContent, '+3.5');
  assert.equal(dom.window.document.querySelector(`${channel} [data-aux="1"] + output`).textContent, '64');
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
