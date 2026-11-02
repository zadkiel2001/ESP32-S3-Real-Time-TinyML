import numpy as np
import tensorflow as tf

# -----------------------------
# Load FP32 Keras model
# -----------------------------
fp32_model = tf.keras.models.load_model(
    "models/har_baseline_fp32.keras"
)

# -----------------------------
# Load INT8 TFLite model
# -----------------------------
int8_interpreter = tf.lite.Interpreter(
    model_path="models/har_int8.tflite"
)
int8_interpreter.allocate_tensors()

int8_input = int8_interpreter.get_input_details()[0]
int8_output = int8_interpreter.get_output_details()[0]

# -----------------------------
# Create one HAR-like input
# Shape: (1, 128, 6)
# -----------------------------
x_fp32 = np.random.randn(1, 128, 6).astype(np.float32)

# -----------------------------
# Run FP32 inference
# -----------------------------
fp32_pred = fp32_model.predict(x_fp32)
fp32_class = int(np.argmax(fp32_pred))

# -----------------------------
# Quantize input for INT8
# -----------------------------
scale, zero_point = int8_input["quantization"]
x_int8 = (x_fp32 / scale + zero_point).astype(np.int8)

# -----------------------------
# Run INT8 inference
# -----------------------------
int8_interpreter.set_tensor(int8_input["index"], x_int8)
int8_interpreter.invoke()
int8_pred = int8_interpreter.get_tensor(int8_output["index"])
int8_class = int(np.argmax(int8_pred))

# -----------------------------
# Print results
# -----------------------------
print("========== FP32 MODEL ==========")
print("Output probabilities:", fp32_pred)
print("Predicted class:", fp32_class)

print("\n========== INT8 MODEL ==========")
print("Quantization scale:", scale)
print("Quantization zero-point:", zero_point)
print("Raw INT8 output:", int8_pred)
print("Predicted class:", int8_class)
