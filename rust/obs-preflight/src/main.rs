use obs_preflight::{DEFAULT_WINDOW_DURATION_MS, StatSample, judge_window, roll_up_windows};
use std::env;
use std::error::Error;
use std::fs::File;
use std::io::{self, BufRead, BufReader, ErrorKind};
use std::process::ExitCode;

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("obs-preflight: {error}");
            ExitCode::FAILURE
        }
    }
}

fn run() -> Result<(), Box<dyn Error>> {
    let mut args = env::args().skip(1);
    let command = args.next().ok_or_else(usage_error)?;
    if command != "replay" {
        return Err(usage_error().into());
    }
    let trace_path = args.next().ok_or_else(usage_error)?;
    let mut window_duration_ms = DEFAULT_WINDOW_DURATION_MS;
    while let Some(argument) = args.next() {
        match argument.as_str() {
            "--window-ms" => {
                let value = args.next().ok_or_else(usage_error)?;
                window_duration_ms = value.parse::<u64>().map_err(|error| {
                    io::Error::new(
                        ErrorKind::InvalidInput,
                        format!("invalid --window-ms value {value:?}: {error}"),
                    )
                })?;
            }
            _ => return Err(usage_error().into()),
        }
    }
    if window_duration_ms == 0 {
        return Err(io::Error::new(
            ErrorKind::InvalidInput,
            "--window-ms must be greater than zero",
        )
        .into());
    }

    let samples = if trace_path == "-" {
        parse_samples(BufReader::new(io::stdin()), "stdin")?
    } else {
        let file = File::open(&trace_path)?;
        parse_samples(BufReader::new(file), &trace_path)?
    };
    let windows = roll_up_windows(&samples, window_duration_ms)?
        .into_iter()
        .map(judge_window)
        .collect::<Vec<_>>();
    let output = serde_json::json!({ "windows": windows });
    println!("{}", serde_json::to_string_pretty(&output)?);
    Ok(())
}

fn parse_samples<R: BufRead>(reader: R, source: &str) -> io::Result<Vec<StatSample>> {
    let mut samples = Vec::new();
    for (line_index, line) in reader.lines().enumerate() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }
        let sample = serde_json::from_str(&line).map_err(|error| {
            io::Error::new(
                ErrorKind::InvalidData,
                format!("{source}:{}: {error}", line_index + 1),
            )
        })?;
        samples.push(sample);
    }
    Ok(samples)
}

fn usage_error() -> io::Error {
    io::Error::new(
        ErrorKind::InvalidInput,
        "usage: obs-preflight replay <trace.jsonl|-> [--window-ms <milliseconds>]",
    )
}
