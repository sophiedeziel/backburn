class Exporter
  def export(format)
    public_send(format)     # public methods only
  end

  def csv = nil             # uncertain

  private

  def secret = nil          # dead: public_send can't call a private method
end
