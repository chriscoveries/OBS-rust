use obs_preflight::{
    AnalysisError, DEFAULT_WINDOW_DURATION_MS, StatSample, judge_window, roll_up_windows,
};
use std::env;
use std::error::Error;
use std::ffi::{OsStr, OsString};
use std::fs::File;
use std::io::{self, BufRead, BufReader, ErrorKind, Write};
use std::path::Path;
use std::process::ExitCode;

#[cfg(test)]
mod platform;

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
    let parsed = parse_args(env::args_os().skip(1))?;

    let samples = if parsed.trace_path == OsStr::new("-") {
        parse_samples(BufReader::new(io::stdin()), "stdin")?
    } else {
        let file = File::open(&parsed.trace_path)?;
        let source = Path::new(&parsed.trace_path).display().to_string();
        parse_samples(BufReader::new(file), &source)?
    };
    let rollups = roll_up_windows(&samples, parsed.window_duration_ms)?;
    if rollups.is_empty() {
        return Err(AnalysisError::NoMeasurableInterval.into());
    }
    let windows = rollups.into_iter().map(judge_window).collect::<Vec<_>>();
    let output = serde_json::json!({ "windows": windows });
    let mut stdout = io::stdout().lock();
    writeln!(stdout, "{}", serde_json::to_string_pretty(&output)?)?;
    Ok(())
}

#[derive(Debug)]
struct ParsedArgs {
    trace_path: OsString,
    window_duration_ms: u64,
}

fn parse_args(mut args: impl Iterator<Item = OsString>) -> Result<ParsedArgs, io::Error> {
    let command = unicode_argument(args.next().ok_or_else(usage_error)?, "command")?;
    if command != "replay" {
        return Err(usage_error());
    }
    let trace_path = args.next().ok_or_else(usage_error)?;
    let mut window_duration_ms = DEFAULT_WINDOW_DURATION_MS;
    while let Some(argument) = args.next() {
        let argument = unicode_argument(argument, "option")?;
        match argument.as_str() {
            "--window-ms" => {
                let value = unicode_argument(args.next().ok_or_else(usage_error)?, "window size")?;
                window_duration_ms = value.parse::<u64>().map_err(|error| {
                    io::Error::new(
                        ErrorKind::InvalidInput,
                        format!("invalid --window-ms value {value:?}: {error}"),
                    )
                })?;
            }
            _ => return Err(usage_error()),
        }
    }
    if window_duration_ms == 0 {
        return Err(io::Error::new(
            ErrorKind::InvalidInput,
            "--window-ms must be greater than zero",
        ));
    }

    Ok(ParsedArgs {
        trace_path,
        window_duration_ms,
    })
}

fn unicode_argument(argument: OsString, label: &str) -> Result<String, io::Error> {
    argument.into_string().map_err(|_| {
        io::Error::new(
            ErrorKind::InvalidInput,
            format!("{label} must be valid Unicode"),
        )
    })
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

#[cfg(test)]
mod tests {
    use super::parse_args;
    use std::ffi::OsString;
    use std::io::ErrorKind;

    #[test]
    fn parser_returns_an_input_error_for_invalid_window_values() {
        let error = parse_args(
            [
                OsString::from("replay"),
                OsString::from("-"),
                OsString::from("--window-ms"),
                OsString::from("zero"),
            ]
            .into_iter(),
        )
        .expect_err("invalid numeric value should be rejected");
        assert_eq!(error.kind(), ErrorKind::InvalidInput);
    }
}
