
async function GetVideoContents_ffmpeg(event, mimeType)
{
	return await ProcessVideo_ffmpeg(event, mimeType, async (input_path) =>
	{
		let metadata = await GetVideoMetadata_ffmpeg(input_path);

        // Extract frames using FFmpeg
        await ffmpegOperation('EXEC', {
            args: [
                '-i', input_path,
                '-f', 'rawvideo',
                '-pix_fmt', 'rgba',
                'output.raw'
            ]
        });

        try
        {
            let data = await ffmpegOperation('READ_FILE', { path: 'output.raw', encoding: 'binary' });

			const width = metadata.streams[0].width;
			const height = metadata.streams[0].height;
        	const frameSize = width * height * 4;
        	const nb_frames = data.length / frameSize;

        	return {
        		data: data,
        		width: width,
        		height: height,
        		frameSize: frameSize,
        		ndb_frames: nb_frames
        	};
        }
        catch(error) { throw error; }
        finally
        {
            await ffmpegOperation('DELETE_FILE', { path: 'output.raw' });
        }
	});
}

async function ProcessVideo_ffmpeg(event, mimeType, cb)
{
	const mimeToExt = {
	    'video/webm': 'webm',
	    'video/mp4': 'mp4',
	    'video/ogg': 'ogg',
	    'video/quicktime': 'mov',
	    'video/x-msvideo': 'avi',
	    'video/x-matroska': 'mkv',
	    // Add more mappings as needed
	};

    if(ffmpeg_worker == null) {
    	console.log("starting ffmpeg...");
        ffmpeg_worker = new Worker('scripts/ffmpeg-worker.js');
        await ffmpegOperation('LOAD', {});
    }

	const extension = mimeToExt[mimeType] || 'webm'; // Default to 'webm' if mimeType is unrecognized
    const input_path = `input.${extension}`;

    await ffmpegOperation('WRITE_FILE', {
        path: input_path,
        data: new Uint8Array(event.target.result)
    });

    try
    {
    	return await cb(input_path)
    }
    catch(error) { throw error; }
    finally
    {
    	await ffmpegOperation('DELETE_FILE', { path: input_path });
    }
}

async function GetVideoMetadata_ffmpeg(input_path)
{
// we know worker is initialized otherwise the input path would be invalid
    if(ff_logger == null)
    {
        ff_logger = new FFmpegLogger();
		ffmpeg_worker.onmessage = (e) => {
			if (e.data.type === 'LOG') {
				ff_logger.log(e.data.data);
			}
		};
    }

	ff_logger.clear();
	ff_logger.start();

	await ffmpegOperation('FFPROBE', {
		args: ['-v', 'error',  // Changed from 'error' to see more output
		       '-show_format',      // Show container-level metadata
       		   '-show_streams',     // Show stream-specific metadata (video, audio, subtitles, etc.)
			   '-of', 'json', input_path]
	});

	ff_logger.stop();

    try {
        let parsedProbe = JSON.parse(ff_logger.getStdout());
        if (!parsedProbe.streams || parsedProbe.streams.length === 0) {
        	console.log(ff_logger.getStdout());
        	console.log(ff_logger.getStderr());
            throw new Error('No video streams found.');
        }

    	return parsedProbe;

    } catch (parseError) {
        throw new Error(`Failed to parse ffprobe result: ${parseError.message}`);
    }
}
