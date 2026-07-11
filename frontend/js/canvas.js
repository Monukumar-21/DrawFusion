class CanvasManager {
    constructor(canvasId) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) return;
        
        this.ctx = this.canvas.getContext('2d');
        this.isDrawing = false;
        
        // Settings
        this.color = '#000000'; // Default black (since canvas background is white)
        this.lineWidth = 5;
        
        this.initEvents();
        this.resize();
        
        // Handle window resize
        window.addEventListener('resize', () => this.resize());
    }
    
    initEvents() {
        // Mouse Events
        this.canvas.addEventListener('mousedown', (e) => this.startDrawing(e));
        this.canvas.addEventListener('mousemove', (e) => this.draw(e));
        this.canvas.addEventListener('mouseup', () => this.stopDrawing());
        this.canvas.addEventListener('mouseout', () => this.stopDrawing());
        
        // Touch Events for mobile
        this.canvas.addEventListener('touchstart', (e) => {
            e.preventDefault();
            this.startDrawing(e.touches[0]);
        }, { passive: false });
        this.canvas.addEventListener('touchmove', (e) => {
            e.preventDefault();
            this.draw(e.touches[0]);
        }, { passive: false });
        this.canvas.addEventListener('touchend', () => this.stopDrawing());
    }
    
    resize() {
        // Only resize if we actually have dimensions
        if (!this.canvas.clientWidth || !this.canvas.clientHeight) return;
        
        // Use a temporary canvas to save the current drawing before resizing
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = this.canvas.width || 1;
        tempCanvas.height = this.canvas.height || 1;
        const tempCtx = tempCanvas.getContext('2d');
        tempCtx.drawImage(this.canvas, 0, 0);
        
        // Set internal dimensions to match the actual display size
        this.canvas.width = this.canvas.clientWidth;
        this.canvas.height = this.canvas.clientHeight;
        
        // Restore white background
        this.clear();
        
        // Restore previous drawing (will be anchored to top-left)
        this.ctx.drawImage(tempCanvas, 0, 0);
    }
    
    startDrawing(e) {
        this.isDrawing = true;
        this.ctx.beginPath();
        
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        
        this.ctx.moveTo(x, y);
    }
    
    draw(e) {
        if (!this.isDrawing) return;
        
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        
        this.ctx.lineTo(x, y);
        this.ctx.strokeStyle = this.color;
        this.ctx.lineWidth = this.lineWidth;
        this.ctx.lineCap = 'round';
        this.ctx.lineJoin = 'round';
        this.ctx.stroke();
    }
    
    stopDrawing() {
        this.isDrawing = false;
        this.ctx.closePath();
    }
    
    setColor(hexColor) {
        this.color = hexColor;
    }
    
    setLineWidth(width) {
        this.lineWidth = width;
    }
    
    clear() {
        this.ctx.fillStyle = '#ffffff';
        this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    }
    
    getBase64() {
        // Returns the canvas image as a base64 string
        return this.canvas.toDataURL('image/png');
    }
}

// Will be initialized in app.js when game screen is shown
window.CanvasManager = CanvasManager;
