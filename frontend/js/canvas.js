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
        const parent = this.canvas.parentElement;
        // Save current image data
        const tempImageData = this.ctx.getImageData(0, 0, this.canvas.width || 1, this.canvas.height || 1);
        
        // Set canvas internal dimensions to match display size
        this.canvas.width = parent.clientWidth - 40; // accounting for padding
        this.canvas.height = parent.clientHeight - 80;
        
        // Restore white background
        this.clear();
        
        // Restore image data if we had any (though resizing usually clears it in games like this)
        // For simplicity, we just clear on resize.
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
