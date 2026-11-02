import os
import numpy as np
import tensorflow as tf

BASE = os.path.join("data", "UCI HAR Dataset")

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
    X = np.stack(signals, axis=-1).astype(np.float32)   # (N,128,6)
    y = np.loadtxt(y_path).astype(np.int64) - 1         # 0..5
    return X, y

def normalize_like_training(X_train, X):
    mean = X_train.mean(axis=(0,1), keepdims=True)
    std  = X_train.std(axis=(0,1), keepdims=True) + 1e-6
    return (X - mean) / std

def fp32_accuracy(model, X, y, batch=256):
    preds = model.predict(X, batch_size=batch, verbose=0)
    y_hat = np.argmax(preds, axis=1)
    return float((y_hat == y).mean())

def int8_accuracy(tflite_path, X_fp32, y_true):
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()
    inp = interpreter.get_input_details()[0]
    out = interpreter.get_output_details()[0]

    scale, zero = inp["quantization"]
    if scale == 0:
        raise RuntimeError("INT8 model input has invalid quantization scale=0")

    correct = 0
    for i in range(len(X_fp32)):
        x = X_fp32[i:i+1]
        x_q = (x / scale + zero).round().astype(np.int8)
        interpreter.set_tensor(inp["index"], x_q)
        interpreter.invoke()
        y_q = interpreter.get_tensor(out["index"])

        # Output is int8; argmax is still valid
        y_hat = int(np.argmax(y_q, axis=1)[0])
        if y_hat == int(y_true[i]):
            correct += 1

    return correct / len(X_fp32)

def main():
    # Load data
    X_train, y_train = load_split("train")
    X_test,  y_test  = load_split("test")

    # Normalize exactly like training
    X_train_n = normalize_like_training(X_train, X_train)
    X_test_n  = normalize_like_training(X_train, X_test)

    # Load models
    fp32 = tf.keras.models.load_model("models/har_baseline_fp32.keras")
    int8_path = "models/har_int8.tflite"

    # Evaluate FP32
    acc_fp32 = fp32_accuracy(fp32, X_test_n, y_test)

    # Evaluate INT8 (loop, slower but simple + correct)
    acc_int8 = int8_accuracy(int8_path, X_test_n, y_test)

    print("==== Accuracy Results (HAR) ====")
    print(f"FP32 test accuracy : {acc_fp32:.4f}")
    print(f"INT8 test accuracy : {acc_int8:.4f}")
    print(f"Accuracy delta     : {(acc_int8 - acc_fp32):+.4f}")

if __name__ == "__main__":
    main()