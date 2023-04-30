#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "alterations.h"

int apply_to_events(song_data_t *song, event_func_t func, void *data) {
    int sum = 0;
    for (int i = 0; i < song->num_tracks; i++) {
        for (int j = 0; j < song->tracks[i].num_events; j++) {
            sum += func(&song->tracks[i].events[j], data);
        }
    }
    return sum;
}

int change_event_octave(event_t *event, int *octaves) {
    if (event->type != NOTE_ON && event->type != NOTE_OFF && event->type != POLY_PRESSURE) {
        return 0;
    }
    
    int note_value = event->midi_data[0] & 0x7F; // extract note value from first byte of MIDI data
    int new_octave = (note_value + (*octaves * 12)) / 12; // calculate new octave based on original note value and number of octaves to change
    
    if (new_octave < 0 || new_octave > 10) { // check if new octave is within valid range
        return 0;
    }
    
    note_value = note_value % 12; // get the new note value within the current octave
    note_value += new_octave * 12; // calculate the final note value in the new octave
    
    event->midi_data[0] &= 0xF0; // clear the note value bits from the first byte of MIDI data
    event->midi_data[0] |= note_value; // set the new note value in the first byte of MIDI data
    
    return 1;
}
int change_event_time(event_t *event, float *multiplier) {
    // Calculate the new delta-time
    uint32_t old_delta_time = event->delta_time;
    uint32_t new_delta_time = (uint32_t) (*multiplier * old_delta_time);

    // Clamp the new delta-time to a maximum of 0x0FFFFFFF
    if (new_delta_time > 0x0FFFFFFF) {
        new_delta_time = 0x0FFFFFFF;
    }

    // Update the event's delta-time if it has changed
    if (old_delta_time != new_delta_time) {
        event->delta_time = new_delta_time;

        // Calculate the difference in bytes for the variable length quantity representation
        int old_bytes = count_varlen_bytes(old_delta_time);
        int new_bytes = count_varlen_bytes(new_delta_time);
        return new_bytes - old_bytes;
    } else {
        return 0;
    }
}
int change_event_instrument(event_t *event, remapping_t remapping) {
    if (event->type != PROGRAM_CHANGE) {
        return 0;
    }
    int old_instrument = event->data[0];
    int new_instrument = remapping[old_instrument];
    event->data[0] = new_instrument;
    return 1;
}
int change_event_note(event_t *event, remapping_t mapping) {
    if (is_note_event(event)) {
        uint8_t note = get_note_from_event(event);
        uint8_t new_note = remap_value(mapping, note);
        if (new_note != note) {
            set_note_in_event(event, new_note);
            return 1;
        }
    }
    return 0;
}
int change_octave(song_data_t *song, int num_octaves) {
    int modified_events = 0;

    // apply change_event_octave function to each event in the song
    modified_events = apply_to_events(song, change_event_octave, &num_octaves);

    return modified_events;
}
int warp_time(song_data_t *song, float multiplier) {
    // Calculate the new time division for the song
    int new_time_division = (int) (song->header->time_division * multiplier);

    // Update the delta time of each event based on the multiplier
    int delta_time_diff = 0;
    for (int i = 0; i < song->num_tracks; i++) {
        track_data_t *track = song->tracks[i];
        for (int j = 0; j < track->num_events; j++) {
            event_t *event = track->events[j];
            int old_delta_time = event->delta_time;
            event->delta_time = (int) (event->delta_time * multiplier);
            delta_time_diff += get_delta_time_length(event->delta_time) - get_delta_time_length(old_delta_time);
        }
    }

    // Update the length of each track based on the new time division and the new delta times
    int length_diff = 0;
    for (int i = 0; i < song->num_tracks; i++) {
        track_data_t *track = song->tracks[i];
        int old_length = track->length;
        track->length = get_track_length(track, new_time_division);
        length_diff += get_variable_length_quantity_length(track->length) - get_variable_length_quantity_length(old_length);
    }

    // Update the song header with the new time division
    song->header->time_division = new_time_division;

    // Calculate the difference in bytes between the new and old representations of the song
    return delta_time_diff + length_diff + get_header_length(song->header) + get_tracks_length(song);
}
int remap_instruments(song_data_t *song, remapping_t mapping)
{
    int num_events_modified = 0;

    for (int i = 0; i < song->num_tracks; i++) {
        track_t *track = &song->tracks[i];
        for (int j = 0; j < track->num_events; j++) {
            event_t *event = &track->events[j];
            if (event->type == EVENT_TYPE_PROGRAM_CHANGE) {
                int old_instrument = event->data[0];
                if (mapping[old_instrument] != -1) {
                    event->data[0] = mapping[old_instrument];
                    num_events_modified++;
                }
            }
        }
    }

    return num_events_modified;
}
int remap_notes(song_data_t *song, remapping_t mapping) {
    int num_modified_events = 0;
    for (int i = 0; i < song->num_tracks; i++) {
        track_t *track = &song->tracks[i];
        for (int j = 0; j < track->num_events; j++) {
            event_t *event = &track->events[j];
            if (is_note_event(event)) {
                uint8_t note = get_note_value(event);
                if (mapping[note] != -1) {
                    set_note_value(event, mapping[note]);
                    num_modified_events++;
                }
            }
        }
    }
    return num_modified_events;
}
void add_round(song_data_t *song, int track_index, int octave_diff, unsigned int delay, uint8_t instrument) {
    assert(track_index >= 0 && track_index < song->num_tracks); // check if track index is valid
    assert(song->format != 2); // check if song format is not 2
    assert(song->num_channels < 16); // check if song has not already used all its channel values
    
    // Find smallest available MIDI channel in the song
    uint8_t available_channel = 0;
    for (int i = 0; i < song->num_tracks; i++) {
        track_t *t = &song->tracks[i];
        for (int j = 0; j < t->event_count; j++) {
            if (t->events[j].type == MIDI_CHANNEL_EVENT && t->events[j].midi_data.channel == available_channel) {
                available_channel++;
                j = -1; // reset the inner loop to check all events again with new available channel
            }
        }
    }
    
    // Create a copy of the track
    track_t *original_track = &song->tracks[track_index];
    track_t *new_track = malloc(sizeof(track_t));
    memcpy(new_track, original_track, sizeof(track_t));
    
    // Change the octave of the new track
    for (int i = 0; i < new_track->event_count; i++) {
        event_t *e = &new_track->events[i];
        if (e->type == NOTE_EVENT) {
            e->note.octave += octave_diff;
        }
    }
    
    // Change the instrument of the new track
    for (int i = 0; i < new_track->event_count; i++) {
        event_t *e = &new_track->events[i];
        if (e->type == MIDI_PROGRAM_CHANGE_EVENT) {
            e->midi_data.program = instrument;
        }
    }
    
    // Change the channel of the new track's MIDI channel events
    for (int i = 0; i < new_track->event_count; i++) {
        event_t *e = &new_track->events[i];
        if (e->type == MIDI_CHANNEL_EVENT) {
            e->midi_data.channel = available_channel;
        }
    }
    
    // Add delay to the new track
    for (int i = 0; i < new_track->event_count; i++) {
        event_t *e = &new_track->events[i];
        e->delta_time += delay;
    }
    
    // Add the new track to the song
    song->tracks[song->num_tracks] = *new_track;
    song->num_tracks++;
    
    // Update song metadata
    song->format = (song->num_tracks > 1) ? 1 : 0; // update song format
    song->division = original_track->division; // set song division to original track's division
    song->total_time += new_track->length; // update song length
    song->num_channels++; // increment channel count
    
    // Free memory allocated for the new track
    free(new_track);
}

