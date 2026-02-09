#!/usr/bin/env python3
"""
Remap a gradient texture from the "looks better" coordinate system to the "is better" one.

vec4 texEffect = texture(u_erosionTexture, texCoord);

float key_press   = u_fadeInDuration * (1.0 - texEffect.r);
float key_release =  u_fadeOutStart + texEffect.g * u_fadeOutDuration;

float normalized_time = clamp(unlerp(key_press, key_release, u_time), 0.0, 1.0);
	
Old system (looks better): UV = (global_time / animation_duration, normalized_time)
New system (is better):    UV = (1.0 - texEffect.r, normalized_time)

Uses the erosion texture to get actual (attack, release) pairs for accurate multisampling.
For each attack value (output column), finds erosion pixels with matching attack and uses
their actual release values instead of uniform sampling.
"""

import argparse
import numpy as np
from PIL import Image


def bilinear_sample(img, x, y):
    """Sample image at floating point coordinates with bilinear interpolation."""
    h, w = img.shape[:2]

    x = np.clip(x, 0, w - 1)
    y = np.clip(y, 0, h - 1)

    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)

    fx = x - x0
    fy = y - y0

    # Expand dims for broadcasting with color channels
    if img.ndim == 3:
        fx = fx[..., np.newaxis]
        fy = fy[..., np.newaxis]

    w00 = (1 - fx) * (1 - fy)
    w10 = fx * (1 - fy)
    w01 = (1 - fx) * fy
    w11 = fx * fy

    return (img[y0, x0] * w00 +
            img[y0, x1] * w10 +
            img[y1, x0] * w01 +
            img[y1, x1] * w11)


def build_red_to_green_map(erosion):
    """
    Build a lookup table mapping each red value (0-255) to its most likely green value.

    Args:
        erosion: Erosion texture where R = inverted attack, G = release

    Returns:
        Array of shape (256,) where index is red value and value is most likely green
    """
    red_channel = erosion[:, :, 0].flatten()
    green_channel = erosion[:, :, 1].flatten()

    # For each red value, collect all green values and find the mean
    red_to_green = np.zeros(256, dtype=np.float64)

    for r in range(256):
        mask = red_channel == r
        if np.any(mask):
            # Use mean of all green values that appear with this red
            red_to_green[r] = np.mean(green_channel[mask])
        else:
            # No samples for this red value - will interpolate later
            red_to_green[r] = np.nan

    # Interpolate missing values
    valid_mask = ~np.isnan(red_to_green)
    if np.any(valid_mask) and not np.all(valid_mask):
        valid_indices = np.where(valid_mask)[0]
        valid_values = red_to_green[valid_mask]
        all_indices = np.arange(256)
        red_to_green = np.interp(all_indices, valid_indices, valid_values)
    elif not np.any(valid_mask):
        # Fallback: linear mapping if no valid samples
        red_to_green = np.linspace(0, 255, 256)

    return red_to_green


def remap_gradient(gradient, erosion, fade_in_duration=2.0, fade_out_duration=2.0,
                   animation_duration=4.0):
    """
    Remap gradient texture from old coordinate system to new using erosion texture.

    Builds a red->green lookup from the erosion texture, then for each output pixel
    at (attack, normalized_time), computes the corresponding source coordinate.

    Args:
        gradient: Input gradient texture (H x W x C)
        erosion: Erosion texture where R = inverted attack, G = release
        fade_in_duration: Duration of fade-in phase
        fade_out_duration: Duration of fade-out phase
        animation_duration: Total animation duration
    """
    grad_h, grad_w = gradient.shape[:2]
    channels = gradient.shape[2] if gradient.ndim == 3 else 1

    fade_out_start = animation_duration - fade_out_duration

    # Build the red->green lookup table
    red_to_green = build_red_to_green_map(erosion)

    print(f"Red to green mapping (sample): R=0->{red_to_green[0]:.1f}, R=127->{red_to_green[127]:.1f}, R=255->{red_to_green[255]:.1f}")

    # Create output
    if gradient.ndim == 3:
        result = np.zeros((grad_h, grad_w, channels), dtype=np.float64)
    else:
        result = np.zeros((grad_h, grad_w), dtype=np.float64)

    # For each output column (attack value, which is 1 - red/255)
    for col in range(grad_w):
        # Output column corresponds to attack = col / (grad_w - 1)
        # Which means red = 255 * (1 - attack) = 255 - col * 255 / (grad_w - 1)
        attack = col / (grad_w - 1) if grad_w > 1 else 0.5
        red_value = int(round(255 * (1 - attack)))
        red_value = np.clip(red_value, 0, 255)

        # Look up the most likely green for this red
        green_value = red_to_green[red_value]
        release = green_value / 255.0

        # Compute timing
        key_press = fade_in_duration * attack
        key_release = fade_out_start + release * fade_out_duration

        # Handle edge case where release is at or before press
        if key_release <= key_press + 0.001:
            key_release = key_press + 0.001

        # For each row (normalized_time from 0 to 1)
        normalized_times = np.linspace(0, 1, grad_h)

        # Compute source X coordinates (global_time / animation_duration)
        # global_time = key_press + normalized_time * (key_release - key_press)
        global_times = key_press + normalized_times * (key_release - key_press)
        src_xs = np.clip(global_times / animation_duration, 0, 1) * (grad_w - 1)

        # Source Y is same as dest Y (normalized_time)
        src_ys = normalized_times * (grad_h - 1)

        # Sample the gradient
        sampled = bilinear_sample(gradient.astype(np.float64), src_xs, src_ys)
        result[:, col] = sampled

    return np.clip(result, 0, 255).astype(np.uint8)




def main():
    parser = argparse.ArgumentParser(
        description='Remap gradient texture to new UV coordinate system using erosion texture'
    )
    parser.add_argument('erosion', type=str, nargs='?', default="/home/anyuser/websites/spehleonlp.github.io/erosion/fxMapInOut-boost.png", help='Path to erosion texture (R=inverted attack, G=release)')
    parser.add_argument('gradient', type=str, nargs='?', default="/home/anyuser/websites/spehleonlp.github.io/erosion/boom_ramp2D.png", help='Path to input gradient texture')
    parser.add_argument('output', type=str, nargs='?', default="/home/anyuser/websites/spehleonlp.github.io/erosion/boom_ramp2D_remapped.png", help='Path for output remapped gradient')
    parser.add_argument('--fade-in', type=float, default=0.5, help='Fade in duration (default: 0.5)')
    parser.add_argument('--fade-out', type=float, default=2.0, help='Fade out duration (default: 2.0)')
    parser.add_argument('--duration', type=float, default=2.0, help='Animation duration (default: 2.0)')

    args = parser.parse_args()

    # Load images
    erosion = np.array(Image.open(args.erosion))
    gradient = np.array(Image.open(args.gradient))

    print(f"Erosion texture: {erosion.shape}")
    print(f"Gradient texture: {gradient.shape}")
    print(f"Parameters: fade_in={args.fade_in}, fade_out={args.fade_out}, duration={args.duration}")

    # Remap
    remapped = remap_gradient(
        gradient,
        erosion,
        fade_in_duration=args.fade_in,
        fade_out_duration=args.fade_out,
        animation_duration=args.duration
    )

    # Save
    Image.fromarray(remapped).save(args.output)
    print(f"Saved remapped gradient to: {args.output}")


if __name__ == '__main__':
    main()
