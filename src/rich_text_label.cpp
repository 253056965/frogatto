#include <algorithm>

#include "foreach.hpp"
#include "label.hpp"
#include "rich_text_label.hpp"
#include "string_utils.hpp"
#include "variant_utils.hpp"
#include "widget_factory.hpp"

namespace gui
{

namespace {
void flatten_recursively(const std::vector<variant>& v, std::vector<variant>* result)
{
	foreach(const variant& item, v) {
		if(item.is_list()) {
			flatten_recursively(item.as_list(), result);
		} else {
			result->push_back(item);
		}
	}
}

}

rich_text_label::rich_text_label(const variant& v, game_logic::formula_callable* e)
	: widget(v,e)
{
	int xpos = 0, ypos = 0;
	int line_height = 0;
	std::vector<variant> items;
	flatten_recursively(v["children"].as_list(), &items);
	foreach(const variant& item, items) {
		const std::string widget_type = item["type"].as_string();
		if(widget_type == "label") {
			const std::vector<std::string> lines = util::split(item["text"].as_string(), '\n', 0);

			variant label_info = deep_copy_variant(item);

			for(int n = 0; n != lines.size(); ++n) {
				if(n != 0) {
					xpos = 0;
					ypos += line_height;
					line_height = 0;
				}

				std::string candidate;
				std::string line = lines[n];
				while(!line.empty()) {
					std::string::iterator space_itor = std::find(line.begin()+1, line.end(), ' ');

					std::string words(line.begin(), space_itor);
					label_info.add_attr_mutation(variant("text"), variant(words));
					widget_ptr label_widget_holder(widget_factory::create(label_info, e));
					label_ptr label_widget(static_cast<label*>(label_widget_holder.get()));

					bool skip_leading_space = false;

					if(xpos != 0 && xpos + label_widget->width() > width()) {
						xpos = 0;
						ypos += line_height;
						line_height = 0;
						skip_leading_space = true;
					}


					candidate = words;

					while(xpos + label_widget->width() < width() && space_itor != line.end()) {
						candidate = words;
						
						space_itor = std::find(space_itor+1, line.end(), ' ');

						words = std::string(line.begin(), space_itor);
						label_widget->set_text(words);
					}

					line.erase(line.begin(), line.begin() + candidate.size());
					if(skip_leading_space && candidate.empty() == false && candidate[0] == ' ') {
						candidate.erase(candidate.begin());
					}
					label_widget->set_text(candidate);
					label_widget->set_loc(xpos, ypos);

					if(label_widget->height()*0.75 > line_height) {
						line_height = label_widget->height()*0.75;
					}

					xpos += label_widget->width();

					children_.push_back(label_widget);
				}
			}
		} else {
			//any widget other than a label
			widget_ptr w(widget_factory::create(item, e));

			if(xpos != 0 && xpos + w->width() > width()) {
				xpos = 0;
				ypos += line_height;
				line_height = 0;
			}

			if(w->height() > line_height) {
				line_height = w->height();
			}

			w->set_loc(xpos, ypos);

			xpos += w->width();

			children_.push_back(w);
		}
	}

	set_dim(width(), ypos + line_height);
}

void rich_text_label::handle_draw() const
{
	glPushMatrix();
	glTranslatef(x() & ~1, y() & ~1, 0.0);

	foreach(const widget_ptr& widget, children_) {
		widget->draw();
	}

	glPopMatrix();
}

bool rich_text_label::handle_event(const SDL_Event& event, bool claimed)
{
	claimed = scrollable_widget::handle_event(event, claimed);

	SDL_Event ev = event;
	normalize_event(&ev);
	reverse_foreach(widget_ptr widget, children_) {
		claimed = widget->process_event(ev, claimed);
	}

	return claimed;
}

variant rich_text_label::get_value(const std::string& key) const
{
	return widget::get_value(key);
}

void rich_text_label::set_value(const std::string& key, const variant& v)
{
	widget::set_value(key, v);
}

}
