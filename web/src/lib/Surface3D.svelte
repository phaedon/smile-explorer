<!--
  A 3D surface z = f(x, y) over a grid, drawn with three.js and orbited by
  dragging. three.js is loaded only when the figure first scrolls into view,
  so pages without one don't pay for it.

  The surface shows shape; it is not the place to read exact numbers. Pair it
  with a 2D view (Heatmap.svelte, or LineChart slices) that the text refers to
  for values. Colours come from the --seq-* ramp tokens, so the surface
  follows the light and dark themes. Axis labels are HTML, in the page's
  fonts, positioned over the canvas.
-->
<script lang="ts">
  import {onDestroy, onMount} from 'svelte';
  import {niceTicks} from './format';

  let {
    xs,
    ys,
    values,
    xLabel,
    yLabel,
    zLabel,
    xFormat = (v: number) => String(v),
    yFormat = (v: number) => String(v),
    zFormat = (v: number) => String(v),
    height = 360,
    points = [],
    ariaLabel,
  }: {
    xs: number[];
    ys: number[];
    // Row by row: the value at ys[i], xs[j] is values[i * xs.length + j].
    values: ArrayLike<number>;
    xLabel: string;
    yLabel: string;
    zLabel: string;
    xFormat?: (v: number) => string;
    yFormat?: (v: number) => string;
    zFormat?: (v: number) => string;
    height?: number;
    // Data points to show against the surface (market quotes, say), as small
    // spheres in the second series colour.
    points?: {x: number; y: number; z: number}[];
    ariaLabel: string;
  } = $props();

  type Label = {text: string; left: number; top: number; title?: boolean};

  let container: HTMLDivElement;
  let status = $state<'waiting' | 'loading' | 'ready' | 'failed'>('waiting');
  let labels = $state<Label[]>([]);

  // three.js objects, created once it has loaded.
  let three: typeof import('three') | null = null;
  let renderer: import('three').WebGLRenderer | null = null;
  let scene: import('three').Scene | null = null;
  let camera: import('three').PerspectiveCamera | null = null;
  let controls: {update(): void; dispose(): void; addEventListener(e: string, f: () => void): void} | null =
    null;
  let surface: import('three').Group | null = null;
  const cleanups: (() => void)[] = [];

  // The surface spans [-1, 1] in x (spot) and z (depth: y data), and
  // [0, HEIGHT] vertically.
  const HEIGHT = 1.1;

  const zRange = $derived.by(() => {
    let lo = Infinity;
    let hi = -Infinity;
    for (let k = 0; k < values.length; ++k) {
      const v = values[k];
      if (Number.isFinite(v)) {
        lo = Math.min(lo, v);
        hi = Math.max(hi, v);
      }
    }
    for (const p of points) {
      if (Number.isFinite(p.z)) {
        lo = Math.min(lo, p.z);
        hi = Math.max(hi, p.z);
      }
    }
    return lo < hi ? [lo, hi] : [lo - 1, lo + 1];
  });

  const nx = (x: number) => -1 + (2 * (x - xs[0])) / (xs[xs.length - 1] - xs[0] || 1);
  const nz = (y: number) => -1 + (2 * (y - ys[0])) / (ys[ys.length - 1] - ys[0] || 1);
  const nh = (z: number) => (HEIGHT * (z - zRange[0])) / (zRange[1] - zRange[0]);

  function token(name: string): string {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }

  function buildSurface() {
    if (!three || !scene) return;
    const T = three;
    if (surface) {
      scene.remove(surface);
      surface.traverse((o) => {
        const mesh = o as import('three').Mesh;
        mesh.geometry?.dispose();
        (mesh.material as import('three').Material | undefined)?.dispose();
      });
    }
    const ramp = Array.from({length: 10}, (_, k) => new T.Color(token(`--seq-${k}`)));
    const colourAt = (t: number) => {
      const f = Math.max(0, Math.min(1, t)) * (ramp.length - 1);
      const k = Math.min(ramp.length - 2, Math.floor(f));
      return ramp[k].clone().lerp(ramp[k + 1], f - k);
    };

    const positions: number[] = [];
    const colours: number[] = [];
    for (let i = 0; i < ys.length; ++i) {
      for (let j = 0; j < xs.length; ++j) {
        const v = values[i * xs.length + j];
        const h = Number.isFinite(v) ? nh(v) : 0;
        positions.push(nx(xs[j]), h, nz(ys[i]));
        const c = colourAt(h / HEIGHT);
        colours.push(c.r, c.g, c.b);
      }
    }
    const indices: number[] = [];
    for (let i = 0; i + 1 < ys.length; ++i) {
      for (let j = 0; j + 1 < xs.length; ++j) {
        const a = i * xs.length + j;
        const b = a + 1;
        const c = a + xs.length;
        const d = c + 1;
        indices.push(a, c, b, b, c, d);
      }
    }
    const geometry = new T.BufferGeometry();
    geometry.setAttribute('position', new T.Float32BufferAttribute(positions, 3));
    geometry.setAttribute('color', new T.Float32BufferAttribute(colours, 3));
    geometry.setIndex(indices);
    const mesh = new T.Mesh(
      geometry,
      new T.MeshBasicMaterial({vertexColors: true, side: T.DoubleSide}),
    );

    // Grid lines over the surface, every few rows and columns, carry the shape.
    const linePoints: number[] = [];
    const every = (n: number) => Math.max(1, Math.round(n / 12));
    const lift = 0.002;
    for (let i = 0; i < ys.length; i += every(ys.length)) {
      for (let j = 0; j + 1 < xs.length; ++j) {
        const p = i * xs.length + j;
        linePoints.push(positions[3 * p], positions[3 * p + 1] + lift, positions[3 * p + 2]);
        linePoints.push(positions[3 * p + 3], positions[3 * p + 4] + lift, positions[3 * p + 5]);
      }
    }
    for (let j = 0; j < xs.length; j += every(xs.length)) {
      for (let i = 0; i + 1 < ys.length; ++i) {
        const p = i * xs.length + j;
        const q = p + xs.length;
        linePoints.push(positions[3 * p], positions[3 * p + 1] + lift, positions[3 * p + 2]);
        linePoints.push(positions[3 * q], positions[3 * q + 1] + lift, positions[3 * q + 2]);
      }
    }
    const lineGeometry = new T.BufferGeometry();
    lineGeometry.setAttribute('position', new T.Float32BufferAttribute(linePoints, 3));
    const lines = new T.LineSegments(
      lineGeometry,
      new T.LineBasicMaterial({color: new T.Color(token('--surface')), transparent: true, opacity: 0.55}),
    );

    // The floor and back walls' edges, as axes.
    const axisGeometry = new T.BufferGeometry();
    axisGeometry.setAttribute(
      'position',
      new T.Float32BufferAttribute(
        [-1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, -1, -1, 0, -1, -1, HEIGHT, -1, -1, 0, -1, 1, 0, -1, -1, 0, -1, -1, 0, 1],
        3,
      ),
    );
    const axes = new T.LineSegments(
      axisGeometry,
      new T.LineBasicMaterial({color: new T.Color(token('--text-muted'))}),
    );

    surface = new T.Group();
    surface.add(mesh, lines, axes);

    if (points.length > 0) {
      const dots = new T.InstancedMesh(
        new T.SphereGeometry(0.03, 12, 8),
        new T.MeshBasicMaterial({color: new T.Color(token('--series-2'))}),
        points.length,
      );
      const matrix = new T.Matrix4();
      points.forEach((p, k) => {
        matrix.makeTranslation(nx(p.x), nh(p.z), nz(p.y));
        dots.setMatrixAt(k, matrix);
      });
      surface.add(dots);
    }
    scene.add(surface);
  }

  // Tick labels in 3D, placed along three edges of the box.
  const tickPoints = $derived.by(() => {
    const out: {text: string; at: [number, number, number]; title?: boolean}[] = [];
    for (const v of niceTicks(xs[0], xs[xs.length - 1], 4)) {
      if (v >= xs[0] && v <= xs[xs.length - 1]) out.push({text: xFormat(v), at: [nx(v), 0, 1.18]});
    }
    for (const v of niceTicks(ys[0], ys[ys.length - 1], 4)) {
      if (v >= ys[0] && v <= ys[ys.length - 1]) out.push({text: yFormat(v), at: [1.2, 0, nz(v)]});
    }
    for (const v of niceTicks(zRange[0], zRange[1], 3)) {
      if (v >= zRange[0] && v <= zRange[1]) out.push({text: zFormat(v), at: [-1.12, nh(v), -1]});
    }
    out.push({text: xLabel, at: [0, 0, 1.45], title: true});
    out.push({text: yLabel, at: [1.55, 0, 0], title: true});
    out.push({text: zLabel, at: [-1.12, HEIGHT + 0.18, -1], title: true});
    return out;
  });

  function render() {
    if (!three || !renderer || !scene || !camera) return;
    renderer.render(scene, camera);
    const width = container.clientWidth;
    const T = three;
    labels = tickPoints.map(({text, at, title}) => {
      const p = new T.Vector3(...at).project(camera!);
      return {text, title, left: ((p.x + 1) / 2) * width, top: ((1 - p.y) / 2) * height};
    });
  }

  function resize() {
    if (!renderer || !camera) return;
    const width = container.clientWidth;
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    render();
  }

  async function load() {
    status = 'loading';
    try {
      const [T, {OrbitControls}] = await Promise.all([
        import('three'),
        import('three/addons/controls/OrbitControls.js'),
      ]);
      three = T;
      renderer = new T.WebGLRenderer({antialias: true, alpha: true});
      renderer.setPixelRatio(Math.min(2, window.devicePixelRatio));
      const canvas = renderer.domElement;
      canvas.setAttribute('aria-hidden', 'true');
      container.prepend(canvas);
      scene = new T.Scene();
      camera = new T.PerspectiveCamera(32, 1, 0.1, 50);
      camera.position.set(3.2, 2.4, 3.6);
      const orbit = new OrbitControls(camera, canvas);
      orbit.target.set(0, HEIGHT / 3, 0);
      orbit.enablePan = false;
      orbit.minDistance = 3;
      orbit.maxDistance = 9;
      // Keep the view above the floor and away from edge-on, where the axis
      // labels would pile up.
      orbit.minPolarAngle = 0.35;
      orbit.maxPolarAngle = 1.25;
      orbit.addEventListener('change', render);
      orbit.update();
      controls = orbit;
      buildSurface();
      status = 'ready';
      resize();

      const observer = new ResizeObserver(resize);
      observer.observe(container);
      cleanups.push(() => observer.disconnect());
      // Re-read the colour tokens when the theme changes.
      const dark = window.matchMedia('(prefers-color-scheme: dark)');
      const onTheme = () => {
        buildSurface();
        render();
      };
      dark.addEventListener('change', onTheme);
      cleanups.push(() => dark.removeEventListener('change', onTheme));
    } catch (e) {
      console.warn('3D view unavailable:', e);
      status = 'failed';
    }
  }

  // Rebuild when the data change.
  $effect(() => {
    void values;
    void xs;
    void ys;
    void points;
    if (status !== 'ready') return;
    buildSurface();
    render();
  });

  onMount(() => {
    const observer = new IntersectionObserver(
      (entries) => {
        if (entries.some((e) => e.isIntersecting)) {
          observer.disconnect();
          load();
        }
      },
      {rootMargin: '200px'},
    );
    observer.observe(container);
    cleanups.push(() => observer.disconnect());
  });

  onDestroy(() => {
    for (const cleanup of cleanups) cleanup();
    controls?.dispose();
    renderer?.dispose();
    renderer?.domElement.remove();
  });
</script>

<div class="surface" bind:this={container} style:height="{height}px" role="img" aria-label={ariaLabel}>
  {#if status !== 'ready'}
    <p class="placeholder">
      {status === 'failed'
        ? 'The 3D view needs WebGL, which isn’t available here. The charts below show the same data.'
        : 'Loading the 3D view…'}
    </p>
  {:else}
    {#each labels as label, k (k)}
      <span class="label" class:title={label.title} style:left="{label.left}px" style:top="{label.top}px"
        >{label.text}</span
      >
    {/each}
    <span class="hint">Drag to rotate</span>
  {/if}
</div>

<style>
  .surface {
    position: relative;
    width: 100%;
    overflow: hidden;
    border-radius: 8px;
    background: var(--surface-raised);
    border: 1px solid var(--border);
    cursor: grab;
  }
  .surface :global(canvas) {
    display: block;
    width: 100%;
    height: 100%;
    touch-action: none;
  }
  .placeholder {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
    margin: 0;
    padding: 16px;
    text-align: center;
    color: var(--text-secondary);
    font: 14px var(--font-ui);
  }
  .label {
    position: absolute;
    transform: translate(-50%, -50%);
    pointer-events: none;
    font: 12px var(--font-ui);
    color: var(--text-muted);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
  }
  .label.title {
    color: var(--text-secondary);
    font-weight: 500;
  }
  .hint {
    position: absolute;
    right: 10px;
    bottom: 8px;
    font: 12px var(--font-ui);
    color: var(--text-muted);
    pointer-events: none;
  }
</style>
