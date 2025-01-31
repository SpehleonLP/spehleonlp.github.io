// progress.js

/**
 * Runs an asynchronous function with a progress modal.
 * @param {Function} asyncFunc - The asynchronous function to execute.
 * @returns {Promise<any>} - The result of the asynchronous function.
 */
async function runWithProgress(asyncFunc) {
    // Get the modal element
    const modal = document.getElementById('progress-modal');

    if (!modal) {
        console.error('Progress modal element not found.');
        return;
    }

    try {
        // Show the modal
        modal.style.display = 'flex'; // Now correctly set to flex only when showing

        // Execute the asynchronous function
        const result = await asyncFunc();

        // Hide the modal after completion
        modal.style.display = 'none';

        return result;
    } catch (error) {
        // Hide the modal in case of error
        modal.style.display = 'none';
        console.error('Error during asynchronous operation:', error);
        throw error; // Re-throw the error after handling
    }
}
