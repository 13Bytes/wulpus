import json
import os
from zipfile import ZipFile

from fastapi import HTTPException
import pandas as pd
import numpy as np
import io

from wulpus.wulpus_config_models import WulpusConfig


def ensure_dir(dir: str) -> None:
    os.makedirs(dir, exist_ok=True)


def check_if_filereq_is_legitimate(req_name: str, system_dir: str, allowed_ending: str) -> str:
    """ Check if the requested file seems plausible.

    Raise HTTPExceptions if invalid.

    Returns:
        str: The validated file path.
    """
    if os.path.sep in req_name or (os.path.altsep and os.path.altsep in req_name) or len(req_name) > 100:
        raise HTTPException(status_code=400, detail="Invalid req_name")
    if not req_name.lower().endswith(allowed_ending):
        raise HTTPException(status_code=400, detail="Invalid file type")
    path = os.path.join(system_dir, req_name)
    if not os.path.isfile(path):
        raise HTTPException(status_code=404, detail="File not found")
    return path


def zip_to_dataframe(path: str):
    with ZipFile(path, 'r') as zf:
        config_raw = json.loads(zf.read('config-0.json').decode('utf-8'))
        df_flat = pd.read_parquet(io.BytesIO(zf.read('data.parquet')))

    # Columns created by save
    meta_cols = {'tx', 'rx', 'aq_number', 'tx_rx_id', 'log_version'}
    sample_cols = [c for c in df_flat.columns if c not in meta_cols]

    # Ensure numeric order for sample columns (they were saved as strings)
    sample_cols = sorted(sample_cols, key=lambda c: int(c))

    # Rebuild `measurement` as a Series per row
    measurements = [
        pd.Series(row[sample_cols].to_numpy(copy=False))
        for _, row in df_flat.iterrows()
    ]

    df = pd.DataFrame({
        'measurement': measurements,
        'tx': df_flat['tx'].tolist(),
        'rx': df_flat['rx'].tolist(),
        'aq_number': df_flat['aq_number'].to_numpy(),
        'tx_rx_id': df_flat['tx_rx_id'].to_numpy() if 'tx_rx_id' in df_flat else np.arange(len(df_flat)),
        'log_version': df_flat['log_version'].to_numpy() if 'log_version' in df_flat else np.full(len(df_flat), 1, dtype=int),
    }, index=df_flat.index)

    config = WulpusConfig.model_validate(config_raw)
    return df, config
