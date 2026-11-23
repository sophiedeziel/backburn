class Report
  def render(kind, attr, part)
    send("#{kind}_section")         # *_section: a tail only
    public_send("cell_#{attr}_html") # cell_*_html: a head and a tail
    send("#{part}_x_#{kind}")       # *_x_*: no literal head or tail
  end

  def summary_section; end
  def totals_section; end
  def section; end       # does not end in _section
  def cell_price_html; end
  def cell_html; end     # too short for cell_ and _html both
  def make_x_twice; end
  def unused_helper; end
end

module Exporters
  class CsvExporter; end
  class ExportHelper; end
end

Report.new.render(:summary, :price, :make)
Object.const_get("Exporters::#{ARGV.first}Exporter") # *Exporter, a constant
