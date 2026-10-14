import { useEffect, useRef, useState } from 'react';
import type { FromWorker, ToWorker } from './protocol';

/** Starts the engine worker once and routes its messages to `onMessage`. */
export function useEngine(onMessage: (msg: FromWorker) => void) {
  const workerRef = useRef<Worker | null>(null);
  const handlerRef = useRef(onMessage);
  handlerRef.current = onMessage;
  const [failed, setFailed] = useState<string | null>(null);

  useEffect(() => {
    const worker = new Worker(new URL('./engine.worker.ts', import.meta.url), { type: 'module' });
    worker.onmessage = (e: MessageEvent<FromWorker>) => handlerRef.current(e.data);
    worker.onerror = (e) => setFailed(`The engine worker stopped: ${e.message || 'unknown error'}`);
    workerRef.current = worker;
    return () => worker.terminate();
  }, []);

  const send = (msg: ToWorker) => workerRef.current?.postMessage(msg);
  return { send, failed };
}
