from wulpus.helper import zip_to_dataframe
import pandas as pd
import json
import time
import io
import os
import glob
import wulpus as wulpus_pkg
import inspect
from zipfile import ZipFile
import numpy as np
from matplotlib import colors
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

# Find the most recent measurement file
measurement_dir = os.path.join(os.path.dirname(
    inspect.getfile(wulpus_pkg)), 'measurements')
if not os.path.exists(measurement_dir):
    raise FileNotFoundError(
        f"Measurements directory not found: {measurement_dir}")

zip_files = glob.glob(os.path.join(measurement_dir, '*.zip'))
if not zip_files:
    raise FileNotFoundError(f"No zip files found in {measurement_dir}")

# Use the most recent file
path = max(zip_files, key=os.path.getctime)
print(f"Using measurement file: {path}")

try:
    df, config = zip_to_dataframe(path)
except Exception as e:
    raise RuntimeError(f"Failed to load data from {path}: {e}")
print(f"columns: {list(df.columns)}")

# Check if measurement column exists and has data
if 'measurement' not in df.columns or df.empty:
    raise ValueError("Something's off with your file")

# Handle case where measurements might be empty or have different formats
measurements = df['measurement'].dropna()

# Convert to numpy array, handling potential inconsistencies
try:
    data_sel = np.stack(measurements.to_numpy())
    max_len = max(len(m) if hasattr(m, '__len__') else 1 for m in measurements)
except ValueError as e:
    print(f"Warning: Could not stack measurements directly: {e}")

# Create interactive 3D plot
fig = plt.figure(figsize=(14, 10))
border = 1000

# Compute max length of measurements to handle varying lengths
max_len = max(len(m) if hasattr(m, '__len__') else 1 for m in measurements)

padded = np.array([
    np.pad(np.array(m, dtype=float),
           (0, max_len - len(m)), constant_values=np.nan)
    if hasattr(m, '__len__') else np.array([m], dtype=float)
    for m in measurements
])

# Create result DataFrame with proper indexing
measurement_df = pd.DataFrame(padded, index=measurements.index)
result = pd.concat(
    [df.drop(columns=['measurement']).loc[measurements.index], measurement_df], axis=1)

# Create 3D surface plot
ax = fig.add_subplot(111, projection='3d')

# Create coordinate grids for 3D plotting
X = np.arange(padded.shape[0])  # Time (measurement index)
Y = np.arange(padded.shape[1])  # Sample index within measurement
X, Y = np.meshgrid(X, Y)

# Transpose padded data to match meshgrid orientation
Z = padded.T

# Create the 3D surface plot
surf = ax.plot_surface(X, Y, Z, 
                      cmap='viridis', 
                      alpha=0.8,
                      linewidth=0,
                      antialiased=True,
                      vmin=-border, 
                      vmax=border)

# Add a color bar
cbar = fig.colorbar(surf, ax=ax, shrink=0.5, aspect=20)
cbar.set_label('ADC digital code', rotation=270, labelpad=15)

# Set labels and title
ax.set_xlabel('Time (measurement index)')
ax.set_ylabel(f'Sample index within measurement (distance of reflection, max {max_len})')
ax.set_zlabel('ADC digital code')
ax.set_title('3D Interactive Measurement Evolution Over Time')

# Set viewing angle for better initial perspective
ax.view_init(elev=30, azim=45)

# Set z-axis limits for better visualization
ax.set_zlim(-border, border)

# Enable interactive rotation and zooming
plt.tight_layout()
plt.show()
