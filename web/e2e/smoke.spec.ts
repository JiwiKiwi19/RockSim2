import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

// End-to-end checks against the real WebAssembly engine in a real browser.

async function chooseScenario(page: Page, title: string) {
  await page.getByRole('button', { name: 'Scene menu' }).click();
  await page.getByRole('menuitemradio', { name: title, exact: true }).click();
}

test('the engine loads, runs and advances simulated time', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/');
  await expect(page.locator('.viewport-canvas')).toBeVisible();
  await page.getByRole('button', { name: 'Run', exact: true }).click();
  await page.waitForTimeout(1500);
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  const steps = Number((await page.locator('.readouts dd').nth(1).innerText()).replace(/,/g, ''));
  expect(steps).toBeGreaterThan(500);
  expect(errors).toEqual([]);
});

test('out-of-range input is rejected with a specific message', async ({ page }) => {
  await page.goto('/');
  await chooseScenario(page, 'Soft body');
  await page.fill('#param-rows', '40');
  await expect(page.getByText('Use a value from 2 to 16.')).toBeVisible();
  await expect(page.getByRole('button', { name: 'Apply and restart' })).toBeDisabled();
});

test('an unstable configuration is detected and stopped', async ({ page }) => {
  await page.goto('/');
  await chooseScenario(page, 'Soft body');
  await page.getByLabel('Explicit Euler').check();
  await page.fill('#dt', '0.004');
  await page.getByRole('button', { name: 'Run', exact: true }).click();
  await expect(page.getByText('The simulation became unstable and was stopped.')).toBeVisible({ timeout: 15000 });
});

test('the convergence study reproduces the theoretical orders in the browser', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Verify menu' }).click();
  await page.getByRole('menuitem', { name: 'Convergence study' }).click();
  await page.getByRole('button', { name: 'Run convergence study' }).click();
  const rows = page.locator('table.results tbody tr');
  await expect(rows).toHaveCount(4, { timeout: 30000 });
  await expect(page.locator('table.results td.pass')).toHaveCount(4);
});

test('the browser engine reproduces the native golden state hash', async ({ page }) => {
  // Same value as tests/test_simulation.cpp GoldenStateHashesAfter3000Steps (spring-oscillator).
  await page.goto('/');
  await chooseScenario(page, 'Damped oscillator');
  for (let i = 0; i < 30; i++) await page.getByRole('button', { name: 'Step 100' }).click();
  await expect(page.locator('.hash code')).toHaveText('71c4b76808506524', { timeout: 10000 });
});

// The oscillator's view box is (-1.75..1.15) x (-1.45..1.45) and is fitted with 40 px padding.
async function oscillatorPoint(page: Page, x: number, y: number) {
  const box = (await page.locator('.win-sim .viewport-canvas').boundingBox())!;
  const s = Math.min((box.width - 80) / 2.9, (box.height - 80) / 2.9);
  return { x: box.x + box.width / 2 + s * (x - -0.3), y: box.y + box.height / 2 - s * y };
}

const readSteps = async (page: Page) => Number((await page.locator('.readouts dd').nth(1).innerText()).replace(/,/g, ''));

test('dragging a body while running grabs it and moves it', async ({ page }) => {
  await page.goto('/');
  await chooseScenario(page, 'Damped oscillator');
  const body = await oscillatorPoint(page, 0.5, 0);
  await page.mouse.click(body.x, body.y);
  await expect(page.getByText('#1', { exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Run', exact: true }).click();
  await page.waitForTimeout(100);
  const now = await oscillatorPoint(page, 0.5, 0);
  await page.mouse.move(now.x, now.y);
  await page.mouse.down();
  const target = await oscillatorPoint(page, 0.85, 0.6);
  await page.mouse.move(target.x, target.y, { steps: 10 });
  await page.waitForTimeout(1200);
  await page.mouse.up();
  const text = await page.locator('.win-inspector .readouts dd').nth(2).innerText();
  const y = Number(text.match(/,\s*([−-]?[\d.]+)\)/)![1].replace('−', '-'));
  expect(Math.abs(y)).toBeGreaterThan(0.05);
});

test('rewinding and running again reproduces the same hash', async ({ page }) => {
  await page.goto('/');
  await chooseScenario(page, 'Damped oscillator');
  for (let i = 0; i < 10; i++) await page.getByRole('button', { name: 'Step 100' }).click();
  await expect(async () => expect(await readSteps(page)).toBe(1000)).toPass();
  const hash = await page.locator('.hash code').first().innerText();
  await page.getByRole('slider', { name: 'Simulation time' }).focus();
  await page.keyboard.press('Home');
  await expect(async () => expect(await readSteps(page)).toBe(0)).toPass();
  for (let i = 0; i < 10; i++) await page.getByRole('button', { name: 'Step 100' }).click();
  await expect(page.locator('.hash code').first()).toHaveText(hash);
  await expect(page.getByText('REPLAY').or(page.getByText('LIVE'))).toBeVisible();
});

test('a paused edit in the past branches into run B', async ({ page }) => {
  await page.goto('/');
  await chooseScenario(page, 'Damped oscillator');
  for (let i = 0; i < 5; i++) await page.getByRole('button', { name: 'Step 100' }).click();
  await page.getByRole('slider', { name: 'Simulation time' }).focus();
  for (let i = 0; i < 3; i++) await page.keyboard.press('Shift+ArrowLeft');
  await expect(page.getByText('REPLAY')).toBeVisible();
  await expect(page.getByRole('region', { name: 'Simulation B', exact: true })).toHaveCount(0);
  const body = await oscillatorPoint(page, 0.5, 0);
  await page.mouse.click(body.x, body.y);
  const here = await page.locator('.win-sim .viewport-canvas').boundingBox();
  expect(here).not.toBeNull();
  const cur = await page.locator('.win-inspector .readouts dd').nth(2).innerText();
  expect(cur).toContain('(');
  // Drag the selected body somewhere else while paused.
  const start = await oscillatorPoint(page, Number(cur.match(/\(([−-]?[\d.]+),/)![1].replace('−', '-')), 0);
  const end = await oscillatorPoint(page, 0.2, 0.5);
  await page.mouse.move(start.x, start.y);
  await page.mouse.down();
  await page.mouse.move(end.x, end.y, { steps: 6 });
  await page.mouse.up();
  await expect(page.getByRole('region', { name: 'Simulation B', exact: true })).toBeVisible();
  await expect(page.getByText('Run B branched off here', { exact: false })).toBeVisible();
});

test('a blown-up run can be rewound', async ({ page }) => {
  await page.goto('/');
  await chooseScenario(page, 'Soft body');
  await page.getByLabel('Explicit Euler').check();
  await page.fill('#dt', '0.004');
  await page.getByRole('button', { name: 'Run', exact: true }).click();
  await expect(page.getByText('The simulation became unstable and was stopped.')).toBeVisible({ timeout: 15000 });
  await page.getByRole('button', { name: 'Rewind 1 s' }).click();
  await expect(page.getByText('The simulation became unstable and was stopped.')).toHaveCount(0);
});

test('the window layout survives a reload', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByRole('region', { name: 'Notes', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Close Notes' }).click();
  await expect(page.getByRole('region', { name: 'Notes', exact: true })).toHaveCount(0);
  await page.reload();
  await expect(page.locator('.viewport-canvas')).toBeVisible();
  await expect(page.getByRole('region', { name: 'Notes', exact: true })).toHaveCount(0);
  await page.getByRole('button', { name: 'Window menu' }).click();
  await page.getByRole('menuitem', { name: 'Reset layout' }).click();
  await expect(page.getByRole('region', { name: 'Notes', exact: true })).toBeVisible();
});
