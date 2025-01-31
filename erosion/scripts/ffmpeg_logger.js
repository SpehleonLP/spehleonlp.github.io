
class FFmpegLogger {
    constructor() {
        this.stdout = [];
        this.stderr = [];
        this.isRecording = false;
    }

    start() {
        this.isRecording = true;
    }

    stop() {
        this.isRecording = false;
    }

    clear() {
        this.stdout = [];
        this.stderr = [];
    }

    log(logData) {
        if (!this.isRecording) return;

        if (logData.type === 'stdout') {
            this.stdout.push(logData.message);
        } else if (logData.type === 'stderr') {
            this.stderr.push(logData.message);
        }
    }

    getStdout() {
        return this.stdout.join('\n');
    }

    getStderr() {
        return this.stderr.join('\n');
    }
}
