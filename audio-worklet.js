class WasapiOutputProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.channels = options.processorOptions.channels || 2;
    this.blockFrames = 1024;
    this.buffer = new Float32Array(this.blockFrames * this.channels);
    this.frames = 0;
  }

  process(inputs, outputs) {
    const input = inputs[0];
    const output = outputs[0];

    if (output) {
      for (const channel of output) channel.fill(0);
    }

    if (!input || !input.length) return true;

    const left = input[0];
    const right = input[1] || left;
    if (!left) return true;

    for (let i = 0; i < left.length; i++) {
      if (this.channels === 1) {
        this.buffer[this.frames] = input.length > 1
          ? (left[i] + right[i]) * 0.5
          : left[i];
      } else {
        const offset = this.frames * 2;
        this.buffer[offset] = left[i];
        this.buffer[offset + 1] = right[i];
      }

      this.frames++;

      if (this.frames >= this.blockFrames) {
        const completed = this.buffer;
        this.port.postMessage(completed, [completed.buffer]);
        this.buffer = new Float32Array(this.blockFrames * this.channels);
        this.frames = 0;
      }
    }

    return true;
  }
}

registerProcessor('wasapi-output', WasapiOutputProcessor);
