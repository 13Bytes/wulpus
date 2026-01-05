from __future__ import annotations
import os
import glob
import inspect
from datetime import datetime
import numpy as np
import pandas as pd
import wulpus as wulpus_pkg
from typing import Tuple, List, Optional
from wulpus.helper import zip_to_dataframe


def flatten_df_measurements(df: pd.DataFrame, sample_crop: Optional[int] = None) -> Tuple[pd.DataFrame, List[str]]:
    """Convert a DataFrame with a 'measurement' column (each row an array/Series)
    into a flat DataFrame where each sample is its own numeric column.

    Returns (flattened_df, measurement_column_names).
    measurement columns are named as decimal strings: '0','1',... matching sample indices.
    """
    if 'measurement' not in df.columns:
        raise ValueError("DataFrame does not contain 'measurement' column")

    # Build a DataFrame of Series so ragged lengths are allowed
    series_list = []
    for m in df['measurement']:
        arr = np.asarray(m, dtype=float)
        if sample_crop is not None:
            arr = arr[:sample_crop]
        # pd.Series will allow varying lengths when assembled into DataFrame
        series_list.append(pd.Series(arr))

    measurement_expanded = pd.DataFrame(series_list, index=df.index)
    # Name the sample columns as strings '0','1',...
    measurement_expanded.columns = [
        str(i) for i in range(measurement_expanded.shape[1])]

    flattened_df = pd.concat(
        [df.drop(columns=['measurement']), measurement_expanded], axis=1)
    # Ensure column names are strings for downstream parquet compat
    flattened_df.columns = [str(c) for c in flattened_df.columns]

    meas_cols: List[str] = [c for c in measurement_expanded.columns]
    return flattened_df, meas_cols


def format_time_index_to_local(dt_index: pd.DatetimeIndex, tz: Optional[str] = None) -> pd.DatetimeIndex:
    """Convert a DatetimeIndex to a timezone-aware index in tz (or local timezone if tz is None).

    The function assumes integer/us timestamps should already have been converted by caller.
    """
    local_tz = datetime.now().astimezone().tzinfo if tz is None else tz
    try:
        if dt_index.tz is None:
            dt_index = dt_index.tz_localize('UTC').tz_convert(local_tz)
        else:
            dt_index = dt_index.tz_convert(local_tz)
    except Exception:
        # best-effort fallback
        try:
            dt_index = pd.to_datetime(dt_index).tz_localize(
                'UTC').tz_convert(local_tz)
        except Exception:
            pass
    return dt_index


def format_time_xticks(ax, index, num_ticks: int = 10, rotation: int = 45, tz: Optional[str] = None):
    """Set x-ticks on ax using `index` (pandas Index of acquisition timestamps).

    Accepts integer microsecond timestamps or datetime-like Index objects.
    Chooses up to `num_ticks` evenly spaced tick positions and formats labels HH:MM:SS.
    """
    n = len(index)
    if n == 0:
        return
    num_ticks = min(num_ticks, n)
    if num_ticks <= 1:
        ax.set_xticks([0])
        ax.set_xticklabels(['Single acquisition'])
        return

    base_frame_indices = np.linspace(0, n - 1, num_ticks, dtype=int)

    idx_vals = index.values
    if np.issubdtype(idx_vals.dtype, np.integer):
        dt_index = pd.to_datetime(idx_vals, unit='us', errors='coerce')
    else:
        dt_index = pd.to_datetime(index, errors='coerce')

    dt_index = format_time_index_to_local(dt_index, tz)

    # Add extra ticks at stitched-log boundaries (large gaps or time going backwards).
    # Note: do NOT treat dt == 0 as a boundary (duplicate timestamps are common).
    boundary_indices: np.ndarray
    # try:
    dt_seconds = pd.Series(dt_index).diff().dt.total_seconds().to_numpy()
    dt_pos = dt_seconds[np.isfinite(dt_seconds) & (dt_seconds > 0)]

    # If we can't infer a typical cadence, don't guess aggressively.
    typical_dt = float(np.median(dt_pos)) if dt_pos.size else float('nan')
    if np.isfinite(typical_dt) and typical_dt > 0:
        gap_threshold = 10.0 * typical_dt
        boundary_indices = np.where(dt_seconds > gap_threshold)[0]
    else:
        boundary_indices = np.array([], dtype=int)
    # except Exception:
    #     boundary_indices = np.array([], dtype=int)

    frame_indices = np.unique(
        np.concatenate([base_frame_indices.astype(int),
                        boundary_indices.astype(int)])
    )
    frame_indices = frame_indices[(frame_indices >= 0) & (frame_indices < n)]
    frame_indices.sort()

    # Drop ticks that land too close together; prefer the later tick in each pair.
    if frame_indices.size > 1:
        avg_spacing = n / max(num_ticks, 1)
        min_spacing = max(1, int(np.floor(0.2 * avg_spacing)))

        original_candidates = frame_indices.copy()

        # Iteratively prune until all neighbors satisfy spacing.
        changed = True
        while changed and frame_indices.size > 1:
            changed = False
            pruned: list[int] = []
            for idx in frame_indices:
                if not pruned or (idx - pruned[-1]) >= min_spacing:
                    pruned.append(int(idx))
                else:
                    pruned[-1] = int(idx)
                    changed = True
            frame_indices = np.array(pruned, dtype=int)

        # If pruning left large gaps, re-insert from the original set while keeping spacing.
        if frame_indices.size > 1:
            available = [int(i) for i in original_candidates if i not in set(
                frame_indices.tolist())]

            while True:
                gaps = np.diff(frame_indices)
                large_gap_pos = np.where(gaps > 2 * min_spacing)[0]
                if large_gap_pos.size == 0 or not available:
                    break

                inserted = False
                for pos in large_gap_pos:
                    left = frame_indices[pos]
                    right = frame_indices[pos + 1]
                    candidates = [i for i in available if left < i < right]
                    if not candidates:
                        continue

                    # Choose the candidate furthest from the edges to keep spacing robust.
                    best = max(candidates, key=lambda i: min(
                        i - left, right - i))

                    if (best - left) >= min_spacing and (right - best) >= min_spacing:
                        frame_indices = np.insert(frame_indices, pos + 1, best)
                        available.remove(best)
                        inserted = True
                        break

                if not inserted:
                    break

    tick_dts = dt_index[frame_indices]
    labels = [
        (dt.strftime('%H:%M:%S') if not pd.isna(dt) else '')
        for dt in tick_dts
    ]
    ax.set_xticks(frame_indices)
    ax.set_xticklabels(labels, rotation=rotation)


def imshow_with_time(
    ax,
    plot_data: np.ndarray,
    index,
    cmap='viridis',
    interpolation='hamming',
    norm=None,
    num_ticks=10,
    tz: Optional[str] = None,
    colorbar_label: str = 'ADC digital code',
    add_colorbar: bool = True,
):
    """Convenience: imshow the plot_data on ax, add colorbar, and set time xticks using index.

    plot_data shape should be (n_samples, n_acq) i.e. rows=samples, cols=acquisitions.
    """
    im = ax.imshow(
        plot_data,
        aspect='auto',
        cmap=cmap,
        interpolation=interpolation,
        norm=norm,
        origin='lower',
    )
    fig = ax.figure
    if add_colorbar:
        fig.colorbar(im, ax=ax, label=colorbar_label)
    format_time_xticks(ax, index=index, num_ticks=num_ticks,
                       rotation=45, tz=tz)
    return im
