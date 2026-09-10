# Coordinates and Survey Control

> Status: Placeholder. A `FIXED` RTK solution does not validate the base coordinate or coordinate transformation.

## Project Coordinate Configuration

Record UTM zone, hemisphere, units, reference frame/datum, coordinate epoch, projection parameters, and grid-to-ground settings.

For new prototype jobs in Zapopan, Jalisco (approximately 20.77° N,
103.41° W), the Survey browser supplies a starter of **WGS84 / UTM zone 13N
(EPSG:32613), metres, no grid-to-ground scale**. This is a convenience default,
not a confirmation of the client's control datum, epoch, or deliverable CRS;
the operator must review and confirm those before saving a job.

## Heights

Document ellipsoidal height, the selected geoid/vertical model, orthometric height, and all conversions.

## Base Control

Document known-point occupation, static/RGNA processing, autonomous survey-in limitations, and acceptance records.

## Antenna Height

Define the antenna reference point, measurement method, pole height, slant/vertical handling, and antenna offsets.

## Localization

Record control points, residuals, transformation method, acceptance limits, and responsible review.

## Exports

Define required coordinate order, precision, units, metadata, file formats, and compatibility with the existing workflow.
