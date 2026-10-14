import { expect, test } from '@playwright/test';

// End-to-end checks against the real WebAssembly engine in a real browser.

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
  await page.getByRole('button', { name: 'Soft body', exact: true }).click();
  await page.fill('#param-rows', '40');
  await expect(page.getByText('Use a value from 2 to 16.')).toBeVisible();
  await expect(page.getByRole('button', { name: 'Apply and restart' })).toBeDisabled();
});

test('an unstable configuration is detected and stopped', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Soft body', exact: true }).click();
  await page.getByLabel('Explicit Euler').check();
  await page.fill('#dt', '0.004');
  await page.getByRole('button', { name: 'Run', exact: true }).click();
  await expect(page.getByText('The simulation became unstable and was stopped.')).toBeVisible({ timeout: 15000 });
});

test('the convergence study reproduces the theoretical orders in the browser', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Verify' }).click();
  await page.getByRole('button', { name: 'Run convergence study' }).click();
  const rows = page.locator('table.results tbody tr');
  await expect(rows).toHaveCount(4, { timeout: 30000 });
  await expect(page.locator('table.results td.pass')).toHaveCount(4);
});

test('the browser engine reproduces the native golden state hash', async ({ page }) => {
  // Same value as tests/test_simulation.cpp GoldenStateHashesAfter3000Steps (spring-oscillator).
  await page.goto('/');
  await page.getByRole('button', { name: 'Damped oscillator', exact: true }).click();
  for (let i = 0; i < 30; i++) await page.getByRole('button', { name: 'Step 100' }).click();
  await expect(page.locator('.hash code')).toHaveText('71c4b76808506524', { timeout: 10000 });
});
