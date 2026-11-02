import os
import numpy as np
import tensorflow as tf

BASE = "data/UCI HAR Dataset"

def load_split(split: str):
    sig_dir = os.path.join(BASE, split, "Inertial Signals")
    y_path  = os.path.join(BASE, split, f"y_{split}.txt")

    # Pick 6 channels (TinyML-friendly)
    signal_files = [
        f"total_acc_x_{split}.txt",
        f"total_acc_y_{split}.txt",
        f"total_acc_z_{split}.txt",
        f"body_gyro_x_{split}.txt",
        f"body_gyro_y_{split}.txt",
        f"body_gyro_z_{split}.txt",
    ]

    signals = []
    for fn in signal_files:
        path = os.path.join(sig_dir, fn)
        # each file: (num_samples, 128)
        s = np.loadtxt(path)
        signals.append(s)

    # Stack into (num_samples, 128, 6)
    X = np.stack(signals, axis=-1).astype(np.float32)

    # Labels are 1..6 in the dataset -> convert to 0..5
    y = np.loadtxt(y_path).astype(np.int64) - 1

    return X, y

X_train, y_train = load_split("train")
X_test,  y_test  = load_split("test")

# Normalize per-channel using TRAIN stats (important for fairness later)
mean = X_train.mean(axis=(0,1), keepdims=True)
std  = X_train.std(axis=(0,1), keepdims=True) + 1e-6
X_train = (X_train - mean) / std
X_test  = (X_test  - mean) / std

num_classes = 6

model = tf.keras.Sequential([
    tf.keras.layers.Input(shape=(128, 6)),
    tf.keras.layers.Conv1D(16, 5, padding="same", activation="relu"),
    tf.keras.layers.MaxPool1D(2),
    tf.keras.layers.Conv1D(32, 5, padding="same", activation="relu"),
    tf.keras.layers.MaxPool1D(2),
    tf.keras.layers.Flatten(),
    tf.keras.layers.Dense(64, activation="relu"),
    tf.keras.layers.Dense(num_classes, activation="softmax"),
])

model.compile(
    optimizer=tf.keras.optimizers.Adam(1e-3),
    loss="sparse_categorical_crossentropy",
    metrics=["accuracy"],
)

callbacks = [
    tf.keras.callbacks.EarlyStopping(monitor="val_accuracy", patience=8, restore_best_weights=True)
]

history = model.fit(
    X_train, y_train,
    validation_split=0.2,
    epochs=50,
    batch_size=64,
    callbacks=callbacks,
)

test_loss, test_acc = model.evaluate(X_test, y_test, verbose=0)
print("Test accuracy:", test_acc)

model.save("models/har_baseline_fp32.keras")

