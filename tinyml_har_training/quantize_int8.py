import os
import numpy as np
import tensorflow as tf

BASE = "data/UCI HAR Dataset"

def load_split(split: str):
    sig_dir = os.path.join(BASE, split, "Inertial Signals")
    y_path  = os.path.join(BASE, split, f"y_{split}.txt")

    signal_files = [
        f"total_acc_x_{split}.txt",
        f"total_acc_y_{split}.txt",
        f"total_acc_z_{split}.txt",
        f"body_gyro_x_{split}.txt",
        f"body_gyro_y_{split}.txt",
        f"body_gyro_z_{split}.txt",
    ]

    signals = [np.loadtxt(os.path.join(sig_dir, fn)) for fn in signal_files]
    X = np.stack(signals, axis=-1).astype(np.float32)
    y = np.loadtxt(y_path).astype(np.int64) - 1
    return X, y

# Load training data (for calibration)
X_train, _ = load_split("train")

# IMPORTANT: must match FP32 training normalization
mean = X_train.mean(axis=(0,1), keepdims=True)
std  = X_train.std(axis=(0,1), keepdims=True) + 1e-6
X_train = (X_train - mean) / std

# Load FP32 model
model = tf.keras.models.load_model("models/har_baseline_fp32.keras")

def representative_dataset():
    for i in range(500):  # 200–1000 is enough
        yield [X_train[i:i+1].astype(np.float32)]

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()

os.makedirs("models", exist_ok=True)
with open("models/har_int8.tflite", "wb") as f:
    f.write(tflite_model)

print("INT8 model saved to models/har_int8.tflite")