// File: gl_script.js

// these get overwritten when we load an image
let recordCanvasWidth = 32;
let recordCanvasHeight = 32;
let renderDelegate = null; // this will be populated by function render(time, isInFrameBuffer) by the time you use it
let glContext = null; // this will contain the gl context from the canvas
let is_making_video = false;
let textures = {};

// Wait for the DOM to load
document.addEventListener('DOMContentLoaded', () => {
    // Initialize WebGL
    const canvas = document.getElementById('glCanvas');
    const fadeInSlider = document.getElementById('fadeInDuration');
    const fadeInValue = document.getElementById('fadeInDurationValue');
    const fadeOutSlider = document.getElementById('fadeOutDuration');
    const fadeOutValue = document.getElementById('fadeOutDurationValue');
    const lifetimeSlider = document.getElementById('lifetime');
    const lifetimeValue = document.getElementById('lifetimeValue');
    const rateSlider = document.getElementById('rate');
    const rateValue = document.getElementById('rateValue');

    const erosionDrop = document.getElementById('erosionTextureDrop');
    const gradientDrop = document.getElementById('gradientDrop');

    const timeSlider = document.getElementById('time-slider');
    const playPauseButton = document.getElementById('play-pause');

    const gl = initWebGL(canvas);
    glContext = gl;

    if (!gl) return;


    // Load shaders
    Promise.all([
        loadShader(gl, gl.VERTEX_SHADER, 'shaders/erosion.vert'),
        loadShader(gl, gl.FRAGMENT_SHADER, 'shaders/erosion.frag')
    ])
    .then(shaders => {
        const shaderProgram = createProgram(gl, shaders[0], shaders[1]);
        gl.useProgram(shaderProgram);

        // Get attribute and uniform locations
        const attribLocations = {
            position: gl.getAttribLocation(shaderProgram, 'a_position'),
            texCoord: gl.getAttribLocation(shaderProgram, 'a_texCoord'),
        };

        const uniformLocations = {
        	u_viewportSize: gl.getUniformLocation(shaderProgram, 'u_viewportSize'),
            u_erosionTexture: gl.getUniformLocation(shaderProgram, 'u_erosionTexture'),
            u_gradient: gl.getUniformLocation(shaderProgram, 'u_gradient'),
            u_fadeInDuration: gl.getUniformLocation(shaderProgram, 'u_fadeInDuration'),
            u_fadeOutDuration: gl.getUniformLocation(shaderProgram, 'u_fadeOutDuration'),
            u_animationDuration: gl.getUniformLocation(shaderProgram, 'u_animationDuration'),
            u_time: gl.getUniformLocation(shaderProgram, 'u_time'),
        };

        // Set up buffers
        const buffers = initBuffers(gl, attribLocations);

        // Initialize textures
        textures = {
            u_erosionTexture: gl.createTexture(),
            u_gradient: gl.createTexture(),
        };

        // Initially load default textures or placeholders
        initializeTexture(gl, textures.u_erosionTexture, ''); // Empty or default
        initializeTexture(gl, textures.u_gradient, ''); // Empty or default

        // Set texture units
        gl.uniform1i(uniformLocations.u_erosionTexture, 0); // Texture unit 0
        gl.uniform1i(uniformLocations.u_gradient, 1);       // Texture unit 1

        // Main render loop
        function render(time, isInFrameBuffer) {
        	if(is_making_video == true && isInFrameBuffer == false)
        		return;

            // Clear the canvas
            if(isInFrameBuffer)
            {
            	 gl.viewport(0, 0, recordCanvasWidth, recordCanvasHeight);
           		 gl.clearColor(0.0, 0.0, 0.0, 0.0); // Black background
           	}
           	else
           	{
            	 gl.viewport(0, 0, canvas.width, canvas.height);
           		 gl.clearColor(0.0, 0.0, 0.0, 1.0); // Black background
           	}
            gl.clear(gl.COLOR_BUFFER_BIT);

			gl.useProgram(shaderProgram);

            // Get slider values
            const fadeInDuration = parseFloat(fadeInSlider.value);
            const fadeOutDuration = parseFloat(fadeOutSlider.value);
            const animationDuration = parseFloat(lifetimeSlider.value);

            // Update uniforms
            if(isInFrameBuffer)
            {
            	gl.uniform2f(uniformLocations.u_viewportSize, 1, 1);
            }
            else
            {
           		gl.uniform2f(uniformLocations.u_viewportSize, canvas.width, canvas.height);
           	}

            gl.uniform1f(uniformLocations.u_fadeInDuration, fadeInDuration);
            gl.uniform1f(uniformLocations.u_fadeOutDuration, fadeOutDuration);
            gl.uniform1f(uniformLocations.u_animationDuration, animationDuration);
            gl.uniform1f(uniformLocations.u_time, time);



            // Bind textures
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, textures.u_erosionTexture);

            gl.activeTexture(gl.TEXTURE1);
            gl.bindTexture(gl.TEXTURE_2D, textures.u_gradient);

            // Draw the quad
            gl.bindBuffer(gl.ARRAY_BUFFER, buffers);
            gl.vertexAttribPointer(attribLocations.position, 2, gl.FLOAT, false, 4 * Float32Array.BYTES_PER_ELEMENT, 0);
            gl.vertexAttribPointer(attribLocations.texCoord, 2, gl.FLOAT, false, 4 * Float32Array.BYTES_PER_ELEMENT, 2 * Float32Array.BYTES_PER_ELEMENT);

            gl.enableVertexAttribArray(attribLocations.position);
            gl.enableVertexAttribArray(attribLocations.texCoord);

            gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        }

        function renderLoop(currentTime)
        {
			render(parseFloat(timeSlider.value), false);
            requestAnimationFrame(renderLoop);
		}

        requestAnimationFrame(renderLoop);

        // Handle texture uploads from the UI
        setupTextureUpload(gl, textures.u_erosionTexture, 'erosionTextureDrop', 0);
        setupTextureUpload(gl, textures.u_gradient, 'gradientDrop', 1);

function UploadTexture(gl, texture, dropAreaId, textureUnit, image)
{
    const dropArea = document.getElementById(dropAreaId);

	if(textureUnit == 0)
	{
		recordCanvasWidth = image.width;
		recordCanvasHeight = image.height;
	}

    gl.activeTexture(gl.TEXTURE0 + textureUnit);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.generateMipmap(gl.TEXTURE_2D);
    dropArea.style.backgroundImage = `url(${image.src})`;;
    dropArea.innerHTML = ''; // Remove the <p> text
}

        UploadTexture(gl, textures.u_erosionTexture, 'erosionTextureDrop', 0, "fxMapOut-boost.png");
        UploadTexture(gl, textures.u_gradient, 'gradientDrop', 1, "boom_ramp2D.png");

        renderDelegate = render;
    })
    .catch(error => {
        console.error('Error loading shaders:', error);
    });
});

/**
 * Initializes WebGL context.
 * @param {string} canvasId - The ID of the canvas element.
 * @returns {WebGLRenderingContext} - The WebGL context.
 */
function initWebGL(canvas) {
    const gl = canvas.getContext('webgl2', { alpha: false}); // Using WebGL2 for GLSL 450
    if (!gl) {
        alert('WebGL2 is not available in your browser.');
        return null;
    }

    // Set the viewport to match the canvas size
    resizeCanvas(gl, canvas);
    window.addEventListener('resize', () => resizeCanvas(gl, canvas));

    return gl;
}

/**
 * Resizes the canvas to fit the window.
 * @param {WebGLRenderingContext} gl - The WebGL context.
 * @param {HTMLCanvasElement} canvas - The canvas element.
 */
function resizeCanvas(gl, canvas) {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
    gl.viewport(0, 0, gl.canvas.width, gl.canvas.height);
}

/**
 * Loads a shader from a file.
 * @param {WebGLRenderingContext} gl - The WebGL context.
 * @param {number} type - The type of shader (gl.VERTEX_SHADER or gl.FRAGMENT_SHADER).
 * @param {string} url - The URL of the shader file.
 * @returns {Promise<WebGLShader>} - A promise that resolves to the compiled shader.
 */
async function loadShader(gl, type, url) {
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`Failed to load shader from ${url}: ${response.statusText}`);
    }
    const shaderSource = await response.text();
    const shader = gl.createShader(type);
    gl.shaderSource(shader, shaderSource);
    gl.compileShader(shader);

    // Check for compilation errors
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        const info = gl.getShaderInfoLog(shader);
        gl.deleteShader(shader);
        throw new Error(`Could not compile shader:\n${info}`);
    }

    return shader;
}

/**
 * Creates a shader program from vertex and fragment shaders.
 * @param {WebGLRenderingContext} gl - The WebGL context.
 * @param {WebGLShader} vertexShader - The compiled vertex shader.
 * @param {WebGLShader} fragmentShader - The compiled fragment shader.
 * @returns {WebGLProgram} - The linked shader program.
 */
function createProgram(gl, vertexShader, fragmentShader) {
    const program = gl.createProgram();
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);

    // Check for linking errors
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        const info = gl.getProgramInfoLog(program);
        gl.deleteProgram(program);
        throw new Error(`Could not link program:\n${info}`);
    }

    return program;
}

/**
 * Initializes buffers for a fullscreen quad.
 * @param {WebGLRenderingContext} gl - The WebGL context.
 * @param {object} attribLocations - The attribute locations.
 * @returns {object} - The buffer objects.
 */
function initBuffers(gl, attribLocations) {
    // Define positions and texture coordinates for a fullscreen quad
    const vertices = new Float32Array([
        // X, Y, Z, U, V
        -1.0, -1.0, 0.0, 1.0, // Bottom-left
         1.0, -1.0, 1.0, 1.0, // Bottom-right
        -1.0,  1.0, 0.0, 0.0, // Top-left
         1.0,  1.0, 1.0, 0.0, // Top-right
    ]);

    // Create and bind the buffer
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.STATIC_DRAW);

	return buffer;
}

/**
 * Initializes a texture with a default or placeholder image.
 * @param {WebGLRenderingContext} gl - The WebGL context.
 * @param {WebGLTexture} texture - The texture object.
 * @param {string} url - The URL of the image to load.
 */
function initializeTexture(gl, texture, url) {
    gl.bindTexture(gl.TEXTURE_2D, texture);

    // Set texture parameters
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT); // Wrap horizontally
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT); // Wrap vertically
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);    // Minification filter
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);    // Magnification filter

    // Initialize with a 1x1 pixel if no image is provided
    const defaultPixel = new Uint8Array([0, 0, 0, 255]); // Black pixel
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, defaultPixel);
}


function UploadTexture(gl, texture, dropAreaId, textureUnit, image)
{
    const dropArea = document.getElementById(dropAreaId);

	if(textureUnit == 0)
	{
		recordCanvasWidth = image.width;
		recordCanvasHeight = image.height;
	}

    gl.activeTexture(gl.TEXTURE0 + textureUnit);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.generateMipmap(gl.TEXTURE_2D);
    dropArea.style.backgroundImage = `url(${image.src})`;;
    dropArea.innerHTML = ''; // Remove the <p> text
}

/**
 * Sets up texture upload functionality for drag-and-drop and click-to-upload.
 * @param {WebGLRenderingContext} gl - The WebGL context.
 * @param {WebGLTexture} texture - The texture object to update.
 * @param {string} dropAreaId - The ID of the drop area element.
 * @param {number} textureUnit - The texture unit to bind the texture to.
 */
function setupTextureUpload(gl, texture, dropAreaId, textureUnit) {
    const dropArea = document.getElementById(dropAreaId);

    dropArea.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropArea.style.borderColor = '#0f0';
    });

    dropArea.addEventListener('dragleave', () => {
        dropArea.style.borderColor = '#fff';
    });


    dropArea.addEventListener('drop', (e) => {
        e.preventDefault();
        dropArea.style.borderColor = '#fff';
        const files = e.dataTransfer.files;
        if (files.length > 0) {
            const file = files[0];
            if (file.type.startsWith('image/')) {
                const reader = new FileReader();
                reader.onload = function(event) {
                    const image = new Image();
                    image.onload = function() {
                    	UploadTexture(gl, texture, dropAreaId, textureUnit, image);
                    }
                    image.src = event.target.result;
                }
                reader.readAsDataURL(file);
            } else {
                alert('Please drop an image file.');
            }
        }
    });

    dropArea.addEventListener('click', () => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = 'image/*';
        input.onchange = e => {
            const file = e.target.files[0];
            if (file && file.type.startsWith('image/')) {
                const reader = new FileReader();
                reader.onload = function(event) {
                    const image = new Image();
                    image.onload = function() {
                    	UploadTexture(gl, texture, dropAreaId, textureUnit, image);
                    }
                    image.src = event.target.result;
                }
                reader.readAsDataURL(file);
            } else {
                alert('Please select an image file.');
            }
        }
        input.click();
    });
}
