# Per-Pixel Timing Effects

## The Problem

Sprite sheets give you artistic control but eat memory fast — a 256×256 effect at 30 frames is 7.5 MB. Uniform fades are cheap but every pixel does the same thing at the same time. Generic.

What you want: each pixel fading in and out on its own schedule. Edges burning away while the center lingers. Text revealing letter by letter. *Choreographed* motion.

## The Solution

Store *when* each pixel appears and disappears instead of *what it looks like* each frame. One small timing texture, one color gradient. The shader reconstructs the animation in real-time.

Think of a piano — the timing texture is sheet music (when to press/release each key), not a recording. Result: 20× smaller than sprite sheets, scales to any resolution, smoother animation.

This works for any dissolve effect: explosions, text reveals, UI transitions, frost creeping across glass, magic shields forming, portals opening.

For implementation details, see [`shader_to_be_copied.glsl`](shader_to_be_copied.glsl).

---

## The Two Panels

### Left: Gradient Ramp

The color palette for your effect. This is a 2D texture where:
- **X axis** = position in the effect (early-appearing vs late-appearing pixels)
- **Y axis** = pixel lifetime (just revealed vs fully mature)

Load an image or use the procedural generators. Effects here modify colors — brightness, contrast, hue shifts.

### Right: Erosion Texture

The timing map. Load a GIF or image sequence and the tool analyzes when pixels appear and disappear, encoding it into RGB channels.

Effects here clean up the timing — blur removes stair-stepping from low frame rates, fluid simulation adds organic movement.

---

## Two Ways to Create

### Option A: From Animation

1. Load a GIF or image sequence of your effect appearing (each frame adds detail)
2. Optionally load a separate disappearance animation
3. The tool converts frame order into timing gradients

**Constraint:** Fade-in frames can only *add* pixels. Fade-out frames can only *remove* pixels. A pixel can't appear, disappear, then reappear — that requires multiple particles.

### Option B: Paint Directly

Create the erosion texture in any image editor:
- **Red channel** = reveal order (dark appears first)
- **Green channel** = dissolve order (dark vanishes first)
- **Blue channel** = edge softness (dark = crisp, bright = soft)

---

## Tips

- **Low frame rate source?** Smart Blur smooths out banding
- **Want organic edges?** Paint noise into the blue channel
- **Effect feels flat?** Add Y-axis variation to your gradient ramp
- **Hard to see what's happening?** Slow the timeline and scrub manually

---

## Exporting

Download both textures and drop them into your game/engine with the shader from [`shader_to_be_copied.glsl`](shader_to_be_copied.glsl). The shader comments explain all the uniforms and how to integrate it.

---

*Technique from [Alkemi Games' Drifting Lands](http://www.youreallyshouldbeplaying.com/a-game-of-tricks-iii-particles-fun-part1/), inspired by Prince of Persia (2008).*
